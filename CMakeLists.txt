cmake_minimum_required(VERSION 3.21)
project(APLGender)

set(CMAKE_CXX_STANDARD 20)

add_executable(APLGender
        APLGender.cpp)

include_directories(../../../utils)

target_link_libraries(APLGender winmm)