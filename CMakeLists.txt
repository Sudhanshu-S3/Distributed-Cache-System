cmake_minimum_required(VERSION 3.20)

project(DistributedCache VERSION 0.1.0 LANGUAGES CXX)

# --- Setup Standards ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Setup Dependencies (vcpkg) ---
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# --- Setup Warnings ---
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ---  Setup Output Paths ---
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Find Libraries ---
find_package(fmt CONFIG REQUIRED)



add_executable(cache_server src/main.cc)
target_link_libraries(cache_server PRIVATE fmt::fmt)

message(STATUS "Build system configured for: ${PROJECT_NAME}")
