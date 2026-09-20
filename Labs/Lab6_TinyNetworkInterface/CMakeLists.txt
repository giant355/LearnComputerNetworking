cmake_minimum_required(VERSION 3.20)

project(tinynet_lab6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_interface src/tiny_interface.cc)
target_include_directories(tiny_interface PUBLIC src)

if(MSVC)
    target_compile_options(tiny_interface PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_interface PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab6_tests tests/lab6_tests.cc)
target_link_libraries(lab6_tests PRIVATE tiny_interface)

if(MSVC)
    target_compile_options(lab6_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab6_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab6_tests COMMAND lab6_tests)
