cmake_minimum_required(VERSION 3.10)

# set the project name
project(hashtable VERSION 1.0 LANGUAGES C)

# testing
enable_testing()
add_test(NAME ht_test COMMAND ht_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

# load factor benchmarks, one executable per threshold (not built by default)
#   cmake --build build --config Release --target bench
add_custom_target(bench)
foreach(lf "1;2" "2;3" "3;4")
    list(GET lf 0 num)
    list(GET lf 1 den)
    set(name bench_${num}_${den})
    add_executable(${name} EXCLUDE_FROM_ALL bench.c hash.c)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS
        HT_LOAD_FACTOR_NUM=${num} HT_LOAD_FACTOR_DEN=${den})
    add_dependencies(bench ${name})
endforeach()

#target_compile_options(hashtable PRIVATE -std=c++11) 