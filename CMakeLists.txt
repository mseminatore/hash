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
# Suppress MSVC deprecation warnings for standard C functions like fopen
target_compile_definitions(ht PRIVATE _CRT_SECURE_NO_WARNINGS)

# add the executable
add_executable(ht_test test.c)
add_subdirectory(${PROJECT_SOURCE_DIR}/testy)
target_link_libraries(ht_test PRIVATE ht testy)
target_compile_definitions(ht_test PRIVATE _CRT_SECURE_NO_WARNINGS)

#target_compile_options(hashtable PRIVATE -std=c++11) 