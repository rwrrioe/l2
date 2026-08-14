function(l2_add_app name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        l2::transport
        l2::protocol
        l2::book
        l2::signals
        l2::warnings
        l2::options
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endfunction()

l2_add_app(l2_live       live_main.cpp)
l2_add_app(l2_replay     replay_main.cpp)
l2_add_app(l2_record     record_main.cpp)
l2_add_app(l2_crosscheck crosscheck_main.cpp)
