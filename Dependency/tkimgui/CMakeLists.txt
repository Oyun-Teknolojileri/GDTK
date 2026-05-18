cmake_minimum_required(VERSION 3.6)

project(imgui)

# Define IM_EXPORT
add_definitions(-DIM_EXPORT)

# Add a preprocessor definition for IMGUI_USER_CONFIG
add_definitions(-DIMGUI_USER_CONFIG="tk_imconfig.h")

# set source files
set(SOURCES
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_demo.cpp
    imgui/imgui.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/backends/imgui_impl_sdl2.cpp
)

# Optional Vulkan backend. find_package(Vulkan) is silent when the SDK isn't available (e.g.
# emscripten); we just skip the backend source + library link in that case. Required-mode would
# hard-fail on web, which is why the previous incarnation broke the web build.
find_package(Vulkan)
if(Vulkan_FOUND)
    list(APPEND SOURCES imgui/backends/imgui_impl_vulkan.cpp)
endif()

# add library target
add_library(imgui SHARED ${SOURCES})

# set target properties
set_target_properties(imgui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON)

# Disable warnings for clang
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(imgui PRIVATE -w)
endif()

#link sdl
link_directories("${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/SDL2")
find_library(SDL2 SDL2)
find_library(SDL2main SDL2main)

target_link_libraries(imgui PRIVATE SDL2 SDL2main)

if(Vulkan_FOUND)
    target_link_libraries(imgui PRIVATE Vulkan::Vulkan)
endif()

# include subdirectories as additional source files
target_include_directories(imgui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../SDL2/include
    ${CMAKE_CURRENT_LIST_DIR}/imgui
    ${CMAKE_CURRENT_LIST_DIR}/imgui/misc/cpp
    ${CMAKE_CURRENT_LIST_DIR}/imgui/backends
)
