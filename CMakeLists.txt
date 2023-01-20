cmake_minimum_required(VERSION 3.24)
project(chatServer C)

set(CMAKE_C_STANDARD 11)

add_executable(chatServer main.c chatServer.c)
