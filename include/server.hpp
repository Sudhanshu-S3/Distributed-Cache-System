#pragma once
#include "socket.hpp"
#include "arena.hpp"
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>
#include <fcntl.h>


class RedisServer
{
private:

    Socket server_socket;
    Socket epoll_fd;
    Arena arena;
    std::unordered_map<std::string, std::string> store;
    std::unordered_map<int, std::unique_ptr<Socket>> clients;
    
    struct ClientBuffer 
    {
        char data[8192];
        size_t len = 0;
    };

    std::unordered_map<int, ClientBuffer> client_buffers;

    //  Set Non-Blocking
    void set_nonblocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void handle_new_connection();
    void handle_client_data(int fd);


public:

    RedisServer(int port);
    void run();

};