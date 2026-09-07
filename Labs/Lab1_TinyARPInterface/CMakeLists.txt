cmake_minimum_required(VERSION 3.20)

project(tinynet_lab1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tiny_arp
    src/tiny_arp.cc
)

target_include_directories(tiny_arp PUBLIC src)

if(MSVC)
    target_compile_options(tiny_arp PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tiny_arp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lab1_tests
    tests/lab1_tests.cc
)

target_link_libraries(lab1_tests PRIVATE tiny_arp)

if(MSVC)
    target_compile_options(lab1_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lab1_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME lab1_tests COMMAND lab1_tests)
