cmake_minimum_required(VERSION 3.20)

project(tinynet_lab3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_reassembler src/tiny_stream.cc src/tiny_reassembler.cc)
target_include_directories(tiny_reassembler PUBLIC src)

if(MSVC)
    target_compile_options(tiny_reassembler PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_reassembler PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab3_tests tests/lab3_tests.cc)
target_link_libraries(lab3_tests PRIVATE tiny_reassembler)

if(MSVC)
    target_compile_options(lab3_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab3_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab3_tests COMMAND lab3_tests)
