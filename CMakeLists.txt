cmake_minimum_required(VERSION 3.10)

# set the project name
project(hashtable)

# add the includes
include_directories(${PROJECT_SOURCE_DIR})

# add the executable
add_executable(hashtable test.c hash.c)

#target_compile_options(hashtable PRIVATE -std=c++11) 