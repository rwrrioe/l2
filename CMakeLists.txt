cmake_minimum_required(VERSION 3.24)
project(l2_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # clang-tidy / clangd

option(L2_BUILD_TESTS   "Build tests"        ON)
option(L2_BUILD_BENCH   "Build benchmarks"   OFF)
option(L2_SANITIZE      "ASan+UBSan"         OFF)
option(L2_TSAN          "TSan (exclusive)"   OFF)
option(L2_CHECK_INVARIANTS "Book invariant checks" ON)
option(L2_HOT_RELEASE   "-fno-exceptions -fno-rtti hot build" OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Dependencies.cmake)

find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG REQUIRED)
find_package(simdjson REQUIRED)

add_subdirectory(common)
add_subdirectory(spsc)
add_subdirectory(utils)
add_subdirectory(protocol)
add_subdirectory(book)
add_subdirectory(transport)
add_subdirectory(signals)
add_subdirectory(app)

if(L2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
