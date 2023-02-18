cmake_minimum_required(VERSION 3.1)
project(MIMUW-FORK)

set(CMAKE_CXX_STANDARD "17")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++17")

add_executable(executor executor.cpp utils.cpp utils.h err.cpp err.h)