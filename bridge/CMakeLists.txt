cmake_minimum_required(VERSION 3.20)
project(abn_bridge_tools LANGUAGES C CXX)

option(ABN_BUILD_TESTS "Build bridge protocol tests" ON)

add_library(abn_bridge_common STATIC
    common/abn_protocol.c
    common/crc32.c
)
target_include_directories(abn_bridge_common PUBLIC common)
target_compile_features(abn_bridge_common PUBLIC c_std_11)
target_compile_options(abn_bridge_common PRIVATE -Wall -Wextra -Wpedantic)

add_executable(abnload host/abnload.cpp)
target_compile_features(abnload PRIVATE cxx_std_17)
target_compile_options(abnload PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(abnload PRIVATE abn_bridge_common)

if(ABN_BUILD_TESTS)
    enable_testing()
    add_executable(abn_bridge_tests tests/test_protocol.cpp)
    target_compile_features(abn_bridge_tests PRIVATE cxx_std_17)
    target_compile_options(abn_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(abn_bridge_tests PRIVATE abn_bridge_common)
    add_test(NAME abn_bridge_tests COMMAND abn_bridge_tests)
endif()
