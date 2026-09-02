cmake_minimum_required(VERSION 3.20)

# 构建脚手架已经准备好。完成 Lab 0 时不需要修改本文件。
project(tinynet_lab0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_net
    src/tiny_net.cc
)

target_include_directories(tiny_net PUBLIC src)

if(MSVC)
    # /utf-8 让 MSVC 正确读取源码中的中文教学注释。
    target_compile_options(tiny_net PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_net PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab0_tests
    tests/lab0_tests.cc
)

target_link_libraries(lab0_tests PRIVATE tiny_net)

if(MSVC)
    target_compile_options(lab0_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab0_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab0_tests COMMAND lab0_tests)
