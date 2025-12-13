#include "server.hpp"
#include <iostream>

int main() 
{
    RedisServer server(6379);
    server.run();
    return 0;
}