cmake_minimum_required(VERSION 3.16)
project(lab_01 C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(lab_01_parent src/server.c)
add_executable(lab_01_child src/client.c)