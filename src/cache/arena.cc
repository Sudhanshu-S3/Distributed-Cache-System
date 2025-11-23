#include "cache/arena.h" // Notice: Clean include path!

namespace cache {

FixedArena::FixedArena(size_t size) : size_(size), offset_(0) 
{
    buffer_.resize(size);
}

void* FixedArena::allocate(size_t bytes) 
{
 
    if (offset_ + bytes > size_) {
        return nullptr;
    }

    void* ptr = buffer_.data() + offset_;


    offset_ += bytes;

    return ptr;
}

void FixedArena::reset() {
    offset_ = 0;
}

size_t FixedArena::used() const {
    return offset_;
}

size_t FixedArena::capacity() const {
    return size_;
}

} // namespace cache