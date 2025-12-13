#include "server.hpp"
#include "parser.hpp"
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

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
    client_buffers[raw_client_fd] = ClientBuffer{};
    
    std::cout << "New client connected: " << raw_client_fd << std::endl;
}

void RedisServer::handle_client_data(int fd)
{
    ClientBuffer& buf = client_buffers[fd];
    
    // Read new data into buffer
    int bytes = read(fd, buf.data + buf.len, sizeof(buf.data) - buf.len);
    
    if (bytes <= 0)
    {
        std::cout << "Client disconnected: " << fd << std::endl;
        epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
        clients.erase(fd);
        client_buffers.erase(fd);
        return;
    }
    
    buf.len += bytes;
    
    // Process all complete commands in buffer
    RESPParser parser(buf.data, buf.len);
    std::vector<std::string_view> tokens;

    std::string response_buffer; 
    response_buffer.reserve(4096);
    
    while (true)
    {
        size_t consumed = parser.try_parse_command(tokens);
        
        if (consumed == 0) 
        {
            if (parser.pos > 0) 
            {
                memmove(buf.data, buf.data + parser.pos, buf.len - parser.pos);
                buf.len -= parser.pos;
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
        send(fd, response_buffer.data(), response_buffer.size(), 0);
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
                handle_client_data(events[i].data.fd);
            }
        }
    }
}