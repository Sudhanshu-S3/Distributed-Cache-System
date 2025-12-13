#pragma once
#include <unistd.h>


struct Socket
{
    int fd = -1;

    Socket() = default;

    explicit Socket(int raw_fd) : fd(raw_fd) {}

    ~Socket()
    {
        if (fd != -1)
        {
            close(fd);
        }
    }

    // Delete Copy Constructor (Unique Ownership)
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Allow Move
    Socket(Socket&& other) noexcept : fd(other.fd)
    {
        other.fd = -1;
    }

    Socket& operator=(Socket&& other) noexcept
    {
        if (this != &other)
        {
            if (fd != -1)
            {
                close(fd);
            }
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const { return fd; }
};