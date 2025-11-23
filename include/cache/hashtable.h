#pragma once
#include <vector>
#include <cstdint>
#include "cache/arena.h"

namespace cache 
{


struct Bucket 
{
    uint64_t key_hash = 0;     // Key ID
    void* value_ptr = nullptr; // Pointer to data in Arena
    bool occupied = false;     
};

class FixedHashTable 
{
public:

    explicit FixedHashTable(size_t capacity);

    bool set(uint64_t key_hash, void* value_ptr);

    [[nodiscard]] void* get(uint64_t key_hash);

private:
    std::vector<Bucket> table_;
    size_t capacity_;
    size_t count_ = 0;
};

} // namespace cache