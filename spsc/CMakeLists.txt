add_library(l2_spsc INTERFACE)
add_library(l2::spsc ALIAS l2_spsc)

target_include_directories(l2_spsc
INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
