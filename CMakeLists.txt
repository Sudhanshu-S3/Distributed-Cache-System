cmake_minimum_required(VERSION 3.10)
project(FlashCache VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include the 'include' directory for headers
include_directories(include)

# Gather all source files
file(GLOB SOURCES "src/*.cpp")

# Create the executable
add_executable(flash_cache ${SOURCES})

# Optimization Flags (CRITICAL for HFT)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(flash_cache PRIVATE -O3 -march=native -Wall -Wextra)
endif()