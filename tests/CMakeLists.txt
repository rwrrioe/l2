add_executable(l2_tests
    test_types.cpp test_sequencer.cpp
    test_book_typed.cpp test_properties.cpp)
target_link_libraries(l2_tests PRIVATE
    l2::book l2::protocol l2::signals GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(l2_tests)
