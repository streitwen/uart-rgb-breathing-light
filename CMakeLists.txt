cmake_minimum_required(VERSION 3.16)
project(breath_led_uart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(breath_sender src/main.cpp)
target_link_libraries(breath_sender PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(breath_sender PRIVATE /W4)
else()
    target_compile_options(breath_sender PRIVATE -Wall -Wextra -Wpedantic)
endif()
