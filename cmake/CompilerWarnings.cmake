add_library(l2_warnings INTERFACE)
add_library(l2::warnings ALIAS l2_warnings)
target_compile_options(l2_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion
    -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast
)

# hotpath options
add_library(l2_options INTERFACE)
add_library(l2::options ALIAS l2_options)
target_compile_options(l2_options INTERFACE
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${L2_HOT_RELEASE}>>:-fno-exceptions -fno-rtti>
)
target_compile_definitions(l2_options INTERFACE
    $<$<BOOL:${L2_CHECK_INVARIANTS}>:L2_CHECK_INVARIANTS=1>
)
