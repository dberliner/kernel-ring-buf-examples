cmake_minimum_required(VERSION 3.0)
project(kernel-ring-buf-examples C)

set(CMAKE_C_STANDARD 99)

add_compile_options(-std=c99 -Wall -Wextra -Wno-missing-field-initializers)

add_executable(ring_buf_single single_byte.c)
add_executable(ring_buf_multi multi_byte.c)
add_executable(ring_buf_consumer consumer_section.c)