cmake_minimum_required(VERSION 3.20)

project(BlockStore CXX)

add_subdirectory("../CppSerialize" CppSerialize)
add_subdirectory("../SQLite3Helper" SQLite3Helper)
add_subdirectory(BlockStore)
add_subdirectory(Test)
