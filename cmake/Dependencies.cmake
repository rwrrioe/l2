include(FetchContent)

# FIND_PACKAGE_ARGS: try find_package() first (picks up Homebrew installs),
# download from GitHub only if no system package is found.
FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.10.1
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES GTest)   # brew's config package is called GTest, not googletest
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS)
# xxHash ships no CMake config in Homebrew, so it is always fetched (cached in build/_deps)
FetchContent_Declare(xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake_unofficial)   # xxHash's CMakeLists isn't at the repo root
FetchContent_Declare (unordered_dense
  GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
  GIT_TAG v4.9.1
  GIT_SHALLOW TRUE
)

# xxHash ships an ancient cmake_minimum_required(<3.5); let new CMake configure it anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(simdjson xxhash unordered_dense)
if(L2_BUILD_TESTS)
    set(BUILD_GMOCK OFF)
    FetchContent_MakeAvailable(googletest)
endif()
if(L2_BUILD_BENCH)
    set(BENCHMARK_ENABLE_TESTING OFF)
    FetchContent_MakeAvailable(benchmark)
endif()
