#pragma once
#include "socket.hpp"
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>
#include <fcntl.h>
#include <vector>
#include <csignal>
#include <sys/signalfd.h>

class RedisServer
{
private:

    Socket server_socket;
    Socket epoll_fd;
    Socket signal_fd;
    bool   running = true;
    std::unordered_map<std::string, std::string> store;

    static constexpr size_t INITIAL_BUF = 8 * 1024;
    static constexpr size_t MAX_BUF     = 64 * 1024 * 1024;

    struct ClientBuffer
    {
        Socket socket;                  // owns the client fd
        std::vector<char> data;
        size_t len = 0;
        std::string write_buf;
        size_t write_pos = 0;
        bool epollout_armed = false;
    };

    std::unordered_map<int, ClientBuffer> clients;

    //  Set Non-Blocking
    bool set_nonblocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    void handle_new_connection();
    void handle_client_data(int fd);
    void try_flush(int fd);
    void handle_client_writable(int fd);
    void arm_epollout(int fd, bool on);


public:

    RedisServer(int port);
    void run();

};