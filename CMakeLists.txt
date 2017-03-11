cmake_minimum_required(VERSION 3.6)
project(Skeleton_Code_2)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        gbn.c
        gbn.h
        receiver.c
        )

add_executable(receiver ${SOURCE_FILES})