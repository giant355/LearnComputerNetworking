cmake_minimum_required(VERSION 3.20)

project(tinynet_lab5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_tcp_sender src/tiny_stream.cc src/wrapping_integers.cc src/tiny_tcp_sender.cc)
target_include_directories(tiny_tcp_sender PUBLIC src)

if(MSVC)
    target_compile_options(tiny_tcp_sender PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_tcp_sender PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab5_tests tests/lab5_tests.cc)
target_link_libraries(lab5_tests PRIVATE tiny_tcp_sender)

if(MSVC)
    target_compile_options(lab5_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab5_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab5_tests COMMAND lab5_tests)
