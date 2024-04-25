cmake_minimum_required(VERSION 3.28)

project(python-host)

set(CMAKE_EXECUTABLE_SUFFIX ".wasm")

add_compile_options(-Werror -Wall -Wextra -Wimplicit-fallthrough)

add_link_options(-Wl,--max-memory=33554432)

add_executable(python-host pyhost.c)

find_package(Python COMPONENTS Development)

target_link_libraries(python-host PUBLIC Python::Python wasi_vfs)
