cmake_minimum_required(VERSION 3.28)
project(multimedia-catalogue)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mmcat
  src/crawler.hpp
  src/crawler.cpp
  src/server.hpp
  src/server.cpp
  src/main.cpp)
target_include_directories(mmcat PRIVATE include)
