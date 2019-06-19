cmake_minimum_required(VERSION 3.12)
project(libtrade)

set(CMAKE_CXX_STANDARD 14)

add_library(libtrade library.cpp library.h utility/container.h utility/container.cpp utility/concurrent.h utility/concurrent.cpp)