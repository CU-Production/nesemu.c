cmake_minimum_required(VERSION 4.0)
project(nesemu_c)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

if (WIN32)
    add_executable(cnesemu WIN32 src/agnes.c src/main.c)
else (WIN32)
    add_executable(cnesemu src/agnes.c src/main.c)
endif (WIN32)
target_link_libraries(cnesemu PRIVATE sokol)
add_custom_command(TARGET cnesemu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:cnesemu>/" "${CMAKE_CURRENT_SOURCE_DIR}/rom/mario.nes"
        COMMAND_EXPAND_LISTS
)
