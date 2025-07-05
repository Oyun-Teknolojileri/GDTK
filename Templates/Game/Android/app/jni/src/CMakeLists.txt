cmake_minimum_required(VERSION 3.6)

project(Game)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-parentheses -Wno-switch")
set(GAME_CODE_DIR "${CMAKE_SOURCE_DIR}/../../../Codes")

# Add the game cmake file.
add_subdirectory(${GAME_CODE_DIR} ${CMAKE_BINARY_DIR}/Codes/Intermediate/${TK_PLATFORM})
