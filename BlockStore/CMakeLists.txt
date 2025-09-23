file(GLOB_RECURSE LIB_SOURCES
    *.h
    *.cpp
)

add_library(BlockStore STATIC ${LIB_SOURCES})
target_include_directories(BlockStore PUBLIC $<PATH:GET_PARENT_PATH,${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(BlockStore PUBLIC CppSerialize)
target_link_libraries(BlockStore PUBLIC SQLite3Helper)
