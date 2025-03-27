cmake_minimum_required(VERSION 3.30)
project(windows_tools)

set(CMAKE_CXX_STANDARD 20)

add_executable(export export.cpp)
add_executable(merge merge.cpp)
