cmake_minimum_required(VERSION 3.10)
project(os-jit)

set(CMAKE_CXX_STANDARD 14)

add_executable(os-jit main.cpp main.cpp InterpreterJIT.h)