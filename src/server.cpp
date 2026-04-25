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
#include <csignal>
#include <sys/signalfd.h>
#include <charconv>

RedisServer::RedisServer(int port)
{
    // 0. Block SIGINT/SIGTERM so we can receive them via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1)
    {
        throw std::runtime_error("sigprocmask failed");
    }

    int raw_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (raw_signal_fd == -1)
    {
        throw std::runtime_error("signalfd creation failed");
    }
    signal_fd = Socket(raw_signal_fd);

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
    if (setsockopt(server_socket.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }


    if (bind(server_socket.get(), (struct sockaddr*)&address, sizeof(address)) == -1)
    {
        throw std::runtime_error("Bind failed");
    }

    if (listen(server_socket.get(), SOMAXCONN) == -1)
    {
        throw std::runtime_error("listen failed");
    }
    if (!set_nonblocking(server_socket.get()))
    {
        throw std::runtime_error("set_nonblocking on listening socket failed");
    }

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
    if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, server_socket.get(), &ev) == -1)
    {
        throw std::runtime_error("epoll_ctl ADD listening socket failed");
    }

    // 5. Add Signalfd to Epoll
    struct epoll_event sig_ev{};
    sig_ev.events = EPOLLIN;
    sig_ev.data.fd = signal_fd.get();
    if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, signal_fd.get(), &sig_ev) == -1)
    {
        throw std::runtime_error("epoll_ctl ADD signalfd failed");
    }

    std::cout << "Server (RAII) listening on port " << port << std::endl;
}



void RedisServer::handle_new_connection()
{
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int raw_client_fd = accept(server_socket.get(), (struct sockaddr*)&client_addr, &len);
    
    if (raw_client_fd == -1)
        return;

    if (!set_nonblocking(raw_client_fd))
    {
        std::cout << "set_nonblocking failed for client " << raw_client_fd << ", closing\n";
        close(raw_client_fd);
        return;
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = raw_client_fd;
    if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, raw_client_fd, &ev) == -1)
    {
        std::cout << "epoll_ctl ADD failed for client " << raw_client_fd << ", closing\n";
        close(raw_client_fd);
        return;
    }

    ClientBuffer cb;
    cb.socket = Socket(raw_client_fd);
    cb.data.resize(INITIAL_BUF);
    clients[raw_client_fd] = std::move(cb);



    
    std::cout << "New client connected: " << raw_client_fd << std::endl;
}

void RedisServer::handle_client_data(int fd)
{
    ClientBuffer& buf = clients[fd];

    // Drain the socket so edge-triggered epoll does not leave unread bytes queued.
    while (true) 
    {
        if (buf.len == buf.data.size()) 
        {
            if (buf.data.size() >= MAX_BUF) 
            {
                // Pathological client — close instead of OOMing
                std::cout << "Client exceeded MAX_BUF, closing: " << fd << std::endl;
                if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr) == -1
                    && errno != ENOENT && errno != EBADF)
                {
                    std::cout << "epoll_ctl DEL warning for fd " << fd
                              << ": errno=" << errno << "\n";
                }
                clients.erase(fd);
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
            if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr) == -1
                && errno != ENOENT && errno != EBADF)
            {
                std::cout << "epoll_ctl DEL warning for fd " << fd
                          << ": errno=" << errno << "\n";
            }
            clients.erase(fd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;

        std::cout << "Client read error: " << fd << std::endl;
        if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr) == -1
            && errno != ENOENT && errno != EBADF)
        {
            std::cout << "epoll_ctl DEL warning for fd " << fd
                      << ": errno=" << errno << "\n";
        }
        clients.erase(fd);
        return;
    }

    // Process all complete commands in buffer
    RESPParser parser(buf.data.data(), buf.len);
    std::vector<std::string_view> tokens;
    
    while (true)
    {
        size_t consumed = parser.try_parse_command(tokens);
        
        if (consumed == 0)
        {
            if (parser.protocol_error)
            {
                std::cout << "Protocol error, closing client: " << fd << std::endl;
                if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr) == -1
                    && errno != ENOENT && errno != EBADF)
                {
                    std::cout << "epoll_ctl DEL warning for fd " << fd
                              << ": errno=" << errno << "\n";
                }
                clients.erase(fd);
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
                buf.write_buf.append("+PONG\r\n");
            }
            else if (cmd == "SET" && tokens.size() >= 3)
            {
                store[std::string(tokens[1])] = std::string(tokens[2]);
                buf.write_buf.append("+OK\r\n");
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
                    char numbuf[24];
                    auto [end, ec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), val.length());
                    buf.write_buf.append("$");
                    buf.write_buf.append(numbuf, end - numbuf);
                    buf.write_buf.append("\r\n");
                    buf.write_buf.append(val);
                    buf.write_buf.append("\r\n");
                } else {
                    buf.write_buf.append("$-1\r\n");
                }

            }
            else 
            {
                buf.write_buf.append("-ERR unknown command\r\n");
            }
        }
    }
    
    // Send everything in ONE System Call
    if (buf.write_pos < buf.write_buf.size())
    {
        try_flush(fd);
    }

}


void RedisServer::run()
{
    static constexpr int MAX_EPOLL_EVENTS = 1024;
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (running)
    {
        int nfds = epoll_wait(epoll_fd.get(), events, MAX_EPOLL_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds && running; ++i)
        {
            int evfd = events[i].data.fd;

            if (evfd == signal_fd.get()) {
                struct signalfd_siginfo si;
                ssize_t r = read(signal_fd.get(), &si, sizeof(si));
                (void)r;
                std::cout << "Received signal " << si.ssi_signo
                          << ", shutting down" << std::endl;
                running = false;
                break;
            }
            else if (evfd == server_socket.get()) {
                handle_new_connection();
            }
            else {
                if (events[i].events & EPOLLIN)  handle_client_data(evfd);
                if (events[i].events & EPOLLOUT) handle_client_writable(evfd);
            }
        }
    }

    // Best-effort flush of pending writes before sockets close
    for (auto& kv : clients) {
        try_flush(kv.first);
    }

    std::cout << "Shutdown complete" << std::endl;
}


void RedisServer::try_flush(int fd)
{
    auto it = clients.find(fd);
    if (it == clients.end()) return;
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

        if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr) == -1
            && errno != ENOENT && errno != EBADF)
        {
            std::cout << "epoll_ctl DEL warning for fd " << fd
                      << ": errno=" << errno << "\n";
        }
        clients.erase(fd);
        return;
    }
    //fully drained - reclaim memory and stop watching EPOLLOUT
    buf.write_buf.clear();
    buf.write_pos = 0;
    arm_epollout(fd, false);
}

void RedisServer::arm_epollout(int fd, bool on)
{
    ClientBuffer& buf = clients[fd];
    if (buf.epollout_armed == on) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | (on ? EPOLLOUT : 0u);
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        std::cout << "epoll_ctl MOD failed for fd " << fd << ", closing client\n";
        clients.erase(fd);   // Socket destructor closes the fd
        return;
    }
    buf.epollout_armed = on;

}

void RedisServer::handle_client_writable(int fd)
{
    try_flush(fd);
}