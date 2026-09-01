add_library(l2_utils INTERFACE)
add_library(l2::utils ALIAS l2_utils)

target_include_directories(l2_utils
INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
