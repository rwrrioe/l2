add_executable(l2_tests
    test_types.cpp test_sequencer.cpp
    test_book_typed.cpp test_properties.cpp)
target_link_libraries(l2_tests PRIVATE
    l2::book l2::protocol l2::signals GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(l2_tests)

# fuzzing
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(fuzz_parser fuzz/fuzz_parser.cpp)
    target_link_libraries(fuzz_parser PRIVATE l2::protocol)
    target_compile_options(fuzz_parser PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_parser PRIVATE -fsanitize=fuzzer,address)
endif()
