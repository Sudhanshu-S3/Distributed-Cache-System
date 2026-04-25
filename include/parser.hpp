#pragma once
#include <vector>
#include <string_view>
#include <cstddef>

// High-performance RESP parser that works on raw buffer
struct RESPParser
{
    const char* data;
    size_t len;
    size_t pos;
    bool protocol_error = false;

    static constexpr int MAX_ARRAY_SIZE = 1024 * 1024;
    static constexpr int MAX_BULK_LEN = 64 * 1024 * 1024;

    RESPParser(const char* d, size_t l) : data(d), len(l), pos(0) {}
    
    // Fast integer parsing
    inline int parse_int(size_t& p) 
    {
        int result = 0;
        bool negative = false;
        
        if (p < len && data[p] == '-') 
        {
            negative = true;
            p++;
        }
        
        while (p < len && data[p] >= '0' && data[p] <= '9') 
        {
            result = result * 10 + (data[p] - '0');
            p++;
        }
        
        return negative ? -result : result;
    }

        bool parse_int_crlf(size_t& p, int& out)
    {
        size_t start = p;
        bool negative = false;

        if (p < len && data[p] == '-') { negative = true; p++; }

        size_t digits_start = p;
        long long result = 0;
        while (p < len && data[p] >= '0' && data[p] <= '9') 
        {
            result = result * 10 + (data[p] - '0');
            if (result > INT32_MAX) { protocol_error = true; p = start; return false; }
            p++;
        }

        if (p == digits_start) 
        {
            if (p >= len) { p = start; return false; }   // incomplete
            protocol_error = true;                       // non-digit where digit expected
            p = start;
            return false;
        }

        if (p + 1 >= len) { p = start; return false; }   // incomplete (need \r\n)
        if (data[p] != '\r' || data[p + 1] != '\n') 
        {
            protocol_error = true;
            p = start;
            return false;
        }
        p += 2;

        out = negative ? -static_cast<int>(result) : static_cast<int>(result);
        return true;
    }

    size_t try_parse_command(std::vector<std::string_view>& tokens)
    {
        tokens.clear();
        size_t start = pos;

        if (pos >= len) return 0;
        if (data[pos] != '*') { protocol_error = true; return 0; }
        pos++;

        int array_size;
        if (!parse_int_crlf(pos, array_size)) { pos = start; return 0; }

        if (array_size < 0 || array_size > MAX_ARRAY_SIZE) 
        {
            protocol_error = true;
            pos = start;
            return 0;
        }

        for (int i = 0; i < array_size; i++) 
        {
            if (pos >= len) { pos = start; tokens.clear(); return 0; }
            if (data[pos] != '$') {
                protocol_error = true;
                pos = start; tokens.clear();
                return 0;
            }
            pos++;

            int str_len;
            if (!parse_int_crlf(pos, str_len)) 
            {
                pos = start; tokens.clear();
                return 0;
            }

            if (str_len < 0 || str_len > MAX_BULK_LEN) 
            {
                protocol_error = true;
                pos = start; tokens.clear();
                return 0;
            }

            if (pos + static_cast<size_t>(str_len) + 2 > len) 
            {
                pos = start; tokens.clear();
                return 0;   // incomplete
            }

            tokens.emplace_back(data + pos, static_cast<size_t>(str_len));
            pos += static_cast<size_t>(str_len) + 2;
        }
        return pos - start;
    }
};