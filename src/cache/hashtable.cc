#include "cache/hashtable.h"

namespace cache {

FixedHashTable::FixedHashTable(size_t capacity) : capacity_(capacity) 
{
    table_.resize(capacity);
}

bool FixedHashTable::set(uint64_t key_hash, void* value_ptr) 
{
    if (count_ >= capacity_) {
        return false;
    }


    size_t index = key_hash % capacity_;
    size_t start_index = index;

    while (true) 
    {
        Bucket& bucket = table_[index];

        
        if (!bucket.occupied) 
        {
            bucket.key_hash = key_hash;
            bucket.value_ptr = value_ptr;
            bucket.occupied = true;
            count_++;
            return true;
        }

        if (bucket.key_hash == key_hash) 
        {
            bucket.value_ptr = value_ptr;
            return true;
        }


        index = (index + 1) % capacity_;

        if (index == start_index) 
        {
            return false;
        }
    }
}

void* FixedHashTable::get(uint64_t key_hash) 
{
    size_t index = key_hash % capacity_;
    size_t start_index = index;

    while (true) 
    {
        Bucket& bucket = table_[index];


        if (!bucket.occupied) 
        {
            return nullptr;
        }


        if (bucket.key_hash == key_hash) 
        {
            return bucket.value_ptr;
        }

        index = (index + 1) % capacity_;

        if (index == start_index) 
        {
            return nullptr;
        }
    }
}

} // namespace cache