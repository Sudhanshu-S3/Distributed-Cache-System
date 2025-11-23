cmake_minimum_required(VERSION 3.20)
project(DistributedCache VERSION 0.1.0 LANGUAGES CXX)

# --- Standards ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Toolchain ---
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# --- Warnings ---
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# --- Dependencies ---
find_package(fmt CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)


add_library(cache_lib 
    src/cache/arena.cc
    src/cache/hashtable.cc
    
)


target_include_directories(cache_lib PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)

# --- The Executable (The Server) ---
add_executable(cache_server src/main.cc)
target_link_libraries(cache_server PRIVATE cache_lib fmt::fmt)

# --- The Tests ---
enable_testing()
include(GoogleTest)


add_executable(unit_tests tests/arena_test.cc)


target_link_libraries(unit_tests PRIVATE cache_lib GTest::gtest_main)

# Register tests
gtest_discover_tests(unit_tests)

message(STATUS "Build configured. Includes mapped to: ${CMAKE_SOURCE_DIR}/include")