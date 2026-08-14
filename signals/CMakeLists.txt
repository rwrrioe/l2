add_library(l2_signals INTERFACE)
add_library(l2::signals ALIAS l2_signals)

target_include_directories(l2_signals INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(l2_signals INTERFACE
    l2::book
    l2::warnings
    l2::options
)
