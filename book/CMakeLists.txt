add_library(l2_book STATIC src/book.cpp)
add_library(l2::book ALIAS l2_book)

target_include_directories(l2_book PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(l2_book
    PUBLIC  l2::common l2::options
    PRIVATE xxhash
    PUBLIC unordered_dense
)
