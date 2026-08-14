# Compiled static library: one .cpp, so STATIC is correct on add_library.
# In every target_* command below, the keyword is a SCOPE
# (PUBLIC/PRIVATE/INTERFACE) — never STATIC.
add_library(l2_protocol STATIC src/parser.cpp)
add_library(l2::protocol ALIAS l2_protocol)

target_include_directories(l2_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(l2_protocol
    PUBLIC  l2::common
    PRIVATE simdjson l2::warnings l2::options)

target_compile_features(l2_protocol PUBLIC cxx_std_20)
