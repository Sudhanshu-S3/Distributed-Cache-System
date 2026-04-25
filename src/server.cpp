#include "server.hpp"
#include "parser.hpp"
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

RedisServer::RedisServer(int port) : arena(64* 1024 * 1024)
{
    // 1. Create Socket
    int raw_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (raw_fd == -1)
    {
        throw std::runtime_error("Socket creation failed");
    }
    server_socket = Socket(raw_fd);
    
    // 2. Bind & Listen
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int opt = 1;
    setsockopt(server_socket.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(server_socket.get(), (struct sockaddr*)&address, sizeof(address)) == -1)
    {
        throw std::runtime_error("Bind failed");
    }
    
    listen(server_socket.get(), SOMAXCONN);
    set_nonblocking(server_socket.get());

    // 3. Create Epoll
    int raw_epoll = epoll_create1(0);
    if (raw_epoll == -1)
    {
        throw std::runtime_error("Epoll creation failed");
    }
    epoll_fd = Socket(raw_epoll);
    
    // 4. Add Server to Epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_socket.get();
    epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, server_socket.get(), &ev);
    
    std::cout << "Server (RAII) listening on port " << port << std::endl;
}


void RedisServer::handle_new_connection()
{
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int raw_client_fd = accept(server_socket.get(), (struct sockaddr*)&client_addr, &len);
    
    if (raw_client_fd == -1)
        return;

    set_nonblocking(raw_client_fd);
    
    // Add to epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = raw_client_fd;
    epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, raw_client_fd, &ev);
    
    // Store in RAII wrapper
    clients[raw_client_fd] = std::make_unique<Socket>(raw_client_fd);
    
    // Initialize buffer
    ClientBuffer cb;
    cb.data.resize(INITIAL_BUF);
    client_buffers[raw_client_fd] = std::move(cb);

    
    std::cout << "New client connected: " << raw_client_fd << std::endl;
}

void RedisServer::handle_client_data(int fd)
{
    ClientBuffer& buf = client_buffers[fd];

    // Drain the socket so edge-triggered epoll does not leave unread bytes queued.
    while (true) 
    {
        if (buf.len == buf.data.size()) 
        {
            if (buf.data.size() >= MAX_BUF) 
            {
                // Pathological client — close instead of OOMing
                std::cout << "Client exceeded MAX_BUF, closing: " << fd << std::endl;
                epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
                clients.erase(fd);
                client_buffers.erase(fd);
                return;
            }
            buf.data.resize(std::min(buf.data.size() * 2, MAX_BUF));
        }

        ssize_t bytes = read(fd, buf.data.data() + buf.len, buf.data.size() - buf.len);

        if (bytes > 0) 
        {
            buf.len += static_cast<size_t>(bytes);
            continue;
        }
        if (bytes == 0) 
        {
            std::cout << "Client disconnected: " << fd << std::endl;
            epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
            clients.erase(fd);
            client_buffers.erase(fd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;

        std::cout << "Client read error: " << fd << std::endl;
        epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
        clients.erase(fd);
        client_buffers.erase(fd);
        return;
    }

    // Process all complete commands in buffer
    RESPParser parser(buf.data.data(), buf.len);
    std::vector<std::string_view> tokens;

    std::string response_buffer; 
    response_buffer.reserve(4096);
    
    while (true)
    {
        size_t consumed = parser.try_parse_command(tokens);
        
        if (consumed == 0)
        {
            if (parser.protocol_error)
            {
                std::cout << "Protocol error, closing client: " << fd << std::endl;
                epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
                clients.erase(fd);
                client_buffers.erase(fd);
                return;
            }
            if (parser.pos > 0)
            {
                memmove(buf.data.data(), buf.data.data() + parser.pos, buf.len - parser.pos);
                buf.len -= parser.pos;

                if (buf.data.size() > INITIAL_BUF && buf.len <= INITIAL_BUF / 2) {
                    buf.data.resize(INITIAL_BUF);
                    buf.data.shrink_to_fit();
                }
            }
            break;
        }

        
        if (!tokens.empty())
        {
            const std::string_view cmd = tokens[0];
            
            if (cmd == "PING") 
            {
                response_buffer.append("+PONG\r\n");
            }
            else if (cmd == "SET" && tokens.size() >= 3) 
            {
                std::string_view key_view = tokens[1];
                std::string_view val_view = tokens[2];

                // 1. Allocate space in Arena for the Value
                char* val_ptr = arena.allocate(val_view.length());

                if (val_ptr) {
                    // 2. Copy data (memcpy is extremely fast)
                    std::memcpy(val_ptr, val_view.data(), val_view.length());

                    // 3. Store the Key (Still allocs key) and View (Zero alloc value)
                    // Note: For absolute max speed, we'd alloc key in Arena too.
                    store[std::string(key_view)] = std::string_view(val_ptr, val_view.length());

                    response_buffer.append("+OK\r\n");
                } else {
                    response_buffer.append("-ERR OOM\r\n"); // Out of Memory
                }
            }

            else if (cmd == "GET" && tokens.size() >= 2) 
            {
                // C++17 trick: find using string_view to avoid key allocation
                // If you are on C++17 or newer, this works directly if using transparent comparator.
                // For now, let's just construct the temp key string as before.
                std::string key(tokens[1]); 
                auto it = store.find(key);

                if (it != store.end()) {
                    std::string_view val = it->second;
                    response_buffer.append("$").append(std::to_string(val.length())).append("\r\n");
                    response_buffer.append(val); // Efficient append
                    response_buffer.append("\r\n");
                } else {
                    response_buffer.append("$-1\r\n");
                }
            }
            else 
            {
                response_buffer.append("-ERR unknown command\r\n");
            }
        }
    }
            
    // Send everything in ONE System Call
    if (!response_buffer.empty()) 
    {
        buf.write_buf.append(response_buffer);
        try_flush(fd);
    }
}


void RedisServer::run()
{
    struct epoll_event events[10];
    
    while (true)
    {
        int nfds = epoll_wait(epoll_fd.get(), events, 10, -1);
        
        for (int i = 0; i < nfds; ++i)
        {
            if (events[i].data.fd == server_socket.get()) 
            {
                handle_new_connection();
            } 
            else 
            {
                if (events[i].events & EPOLLIN)  handle_client_data(events[i].data.fd);
                if (events[i].events & EPOLLOUT) handle_client_writable(events[i].data.fd);
            }

        }
    }
}

void RedisServer::try_flush(int fd)
{
    auto it = client_buffers.find(fd);
    if (it == client_buffers.end()) return;
    ClientBuffer& buf = it->second;

    while (buf.write_pos < buf.write_buf.size())
    {
        const char* p = buf.write_buf.data() + buf.write_pos;
        size_t n = buf.write_buf.size() - buf.write_pos;
        ssize_t w = send( fd, p, n, MSG_NOSIGNAL );

        if (w>0)
        {
            buf.write_pos += static_cast < size_t> (w);
            continue;
        }

        if (w == -1 && errno == EINTR ) continue;
        if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            arm_epollout (fd, true);
            return;
        }
        // real error - close connection

        epoll_ctl( epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
        clients.erase(fd);
        client_buffers.erase(fd);
        return;
    }
    //fully drained - reclaim memory and stop watching EPOLLOUT
    buf.write_buf.clear();
    buf.write_pos = 0;
    arm_epollout(fd, false);
}

void RedisServer::arm_epollout(int fd, bool on)
{
    ClientBuffer& buf = client_buffers[fd];
    if (buf.epollout_armed == on) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | (on ? EPOLLOUT : 0u);
    ev.data.fd = fd;
    epoll_ctl(epoll_fd.get(), EPOLL_CTL_MOD, fd, &ev);
    buf.epollout_armed = on;
}

void RedisServer::handle_client_writable(int fd)
{
    try_flush(fd);
}