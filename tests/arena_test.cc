#include <gtest/gtest.h>

#include "cache/arena.h" 

TEST(ArenaTest, BasicAllocation) 
{
    cache::FixedArena arena(1024);
    void* ptr = arena.allocate(100);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(arena.used(), 100);
}

TEST(ArenaTest, OutOfMemory) 
{
    cache::FixedArena arena(100);
    EXPECT_NE(arena.allocate(90), nullptr); 
    EXPECT_EQ(arena.allocate(20), nullptr);
}

TEST(ArenaTest, Reset) 
{
    cache::FixedArena arena(100);
    void* p1 = arena.allocate(50);
    arena.reset();
    void* p2 = arena.allocate(50);
    EXPECT_EQ(p1, p2); 
}