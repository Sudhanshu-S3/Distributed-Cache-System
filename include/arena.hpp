#pragma once
#include <vector>
#include <iostream>
#include <cstddef>

struct Arena 
{
    std::vector<char> buffer;
    size_t offset;

    Arena(size_t size) : buffer(size), offset(0) 
    {
        // Reserve the memory upfront so it's contiguous
        std::cout << "Arena Initialized: " << size / (1024 * 1024) << " MB" << std::endl;
    }

    // The "Bump Pointer" Allocation - Fast as hell
    char* allocate(size_t size) 
    {
        if (offset + size > buffer.size()) 
        {
            // In a real system, we'd allocate a new block or evict old data.
            // Here, we just return nullptr (OOM).
            return nullptr; 
        }
        char* ptr = buffer.data() + offset;
        offset += size;
        return ptr;
    }
    
    // Reset simply moves pointer back to 0 (Bulk Free)
    void clear() 
    {
        offset = 0;
    }
};