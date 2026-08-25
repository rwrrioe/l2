add_library(l2_common INTERFACE)
add_library(l2::common ALIAS l2_common)
target_include_directories(l2_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# только warnings: l2::options (-fno-exceptions в hot) — контракт горячего
# пути, холодный transport не должен наследовать его через common
target_link_libraries(l2_common INTERFACE l2::warnings)
target_compile_features(l2_common INTERFACE cxx_std_20)
