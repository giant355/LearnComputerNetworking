cmake_minimum_required(VERSION 3.20)

project(tinynet_lab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_stream src/tiny_stream.cc)
target_include_directories(tiny_stream PUBLIC src)

if(MSVC)
    target_compile_options(tiny_stream PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_stream PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab2_tests tests/lab2_tests.cc)
target_link_libraries(lab2_tests PRIVATE tiny_stream)

if(MSVC)
    target_compile_options(lab2_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab2_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab2_tests COMMAND lab2_tests)
