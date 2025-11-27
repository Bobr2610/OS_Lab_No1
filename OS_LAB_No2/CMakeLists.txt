cmake_minimum_required(VERSION 3.16)
project(lab_02 C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(batcher_sort src/main.c)

# Link pthread library on Unix systems
if(UNIX AND NOT APPLE)
    target_link_libraries(batcher_sort pthread)
endif()