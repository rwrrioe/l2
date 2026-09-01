find_package(Boost  REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(l2_transport STATIC
    src/ws_source.cpp
    src/rest_client.cpp
    src/recorder.cpp
    src/file_source.cpp
)
add_library(l2::transport ALIAS l2_transport)

target_include_directories(l2_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(l2_transport
    PUBLIC
        l2::common
        l2::spsc
        l2::utils
    PRIVATE
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        l2::warnings
)
