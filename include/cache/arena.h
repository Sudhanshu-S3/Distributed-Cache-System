#pragma once
#include <cstddef>
#include <vector>
#include <cstdint>

namespace cache {

class FixedArena {
public:
    explicit FixedArena(size_t size);


    FixedArena(const FixedArena&) = delete;
    FixedArena& operator=(const FixedArena&) = delete;

    FixedArena(FixedArena&&) = default;
    FixedArena& operator=(FixedArena&&) = default;

    [[nodiscard]] void* allocate(size_t bytes);

    void reset();
    [[nodiscard]] size_t used() const;
    [[nodiscard]] size_t capacity() const;

private:
    std::vector<std::byte> buffer_;
    size_t size_;
    size_t offset_;
};

} // namespace cache