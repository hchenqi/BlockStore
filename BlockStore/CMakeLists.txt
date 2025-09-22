file(GLOB_RECURSE LIB_SOURCES
    *.h
    *.cpp
)

add_library(BlockStore STATIC ${LIB_SOURCES})
get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
target_include_directories(BlockStore PUBLIC ${PARENT_DIR})
target_link_libraries(BlockStore PUBLIC CppSerialize)
target_link_libraries(BlockStore PUBLIC SQLite3Helper)
