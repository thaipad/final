cmake_minimum_required(VERSION 3.1)

project(final)

find_library(PTHREAD_LIBRARY pthread)

include_directories(include)
set(CMAKE_CXX_STANDARD 11) 
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_executable(final src/final.cpp)

target_link_libraries(final ${PTHREAD_LIBRARY})
