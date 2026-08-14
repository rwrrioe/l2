include(FetchContent)

FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.10.1)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2)
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.0)
FetchContent_Declare(xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
    SOURCE_SUBDIR cmake_unofficial)   # xxHash's CMakeLists isn't at the repo root

# xxHash ships an ancient cmake_minimum_required(<3.5); let new CMake configure it anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(simdjson xxhash)
if(L2_BUILD_TESTS)
    set(BUILD_GMOCK OFF)
    FetchContent_MakeAvailable(googletest)
endif()
if(L2_BUILD_BENCH)
    set(BENCHMARK_ENABLE_TESTING OFF)
    FetchContent_MakeAvailable(benchmark)
endif()
