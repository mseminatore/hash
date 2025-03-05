cmake_minimum_required(VERSION 3.10)

# set the project name
project(hashtable VERSION 1.0 LANGUAGES C)

# testing
enable_testing()
add_test(NAME ht_test COMMAND ht_test)

# add the includes
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/testy)

# add the library
add_library(ht STATIC hash.c)

# add the executable
add_executable(ht_test test.c ${CMAKE_SOURCE_DIR}/testy/test_main.c)
target_link_libraries(ht_test ht)

#target_compile_options(hashtable PRIVATE -std=c++11) 