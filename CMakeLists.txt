cmake_minimum_required(VERSION 3.20)

project(BlockStore CXX)

add_subdirectory("../CppSerialize" CppSerialize)
add_subdirectory("../SQLite3Helper" SQLite3Helper)
add_subdirectory(BlockStore)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    add_subdirectory(Test)
endif()
