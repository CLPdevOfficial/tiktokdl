cmake_minimum_required(VERSION 3.15)
project(MyOpenGLGame LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Setup glfw
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

find_package(OpenGL REQUIRED)

# ImGui needs specific backend files for GLFW and OpenGL3
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

#
add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Downloader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/glad.c
        ${IMGUI_SOURCES}
)

# Header files
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# Linking
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
        glfw
        OpenGL::GL
        Threads::Threads
)