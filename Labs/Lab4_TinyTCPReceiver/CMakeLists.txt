cmake_minimum_required(VERSION 3.20)

project(tinynet_lab4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_tcp_receiver src/tiny_stream.cc src/tiny_reassembler.cc src/wrapping_integers.cc src/tiny_tcp_receiver.cc)
target_include_directories(tiny_tcp_receiver PUBLIC src)

if(MSVC)
    target_compile_options(tiny_tcp_receiver PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_tcp_receiver PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab4_tests tests/lab4_tests.cc)
target_link_libraries(lab4_tests PRIVATE tiny_tcp_receiver)

if(MSVC)
    target_compile_options(lab4_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab4_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab4_tests COMMAND lab4_tests)
