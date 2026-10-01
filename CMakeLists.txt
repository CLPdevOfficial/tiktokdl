cmake_minimum_required(VERSION 3.15)
# RC = Windows resource compiler (embeds favicon.ico into the exe)
project(TikTokDL LANGUAGES CXX C RC)

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

# Icon resource (app.rc + favicon.ico live in the project root).
# Rebuild the resource whenever the icon file changes.
set(APP_RC ${CMAKE_CURRENT_SOURCE_DIR}/app.rc)
set_source_files_properties(${APP_RC} PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/favicon.ico)

# WIN32 = build as a GUI app, so Windows does not open a console window
add_executable(${PROJECT_NAME} WIN32
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Downloader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/glad.c
        ${IMGUI_SOURCES}
        ${APP_RC}
)

# Header files (project root is listed so windres can find favicon.ico)
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        ole32 shell32 uuid     # Shell thumbnail API + COM
)
target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)

# Only needed if you ever build with MSVC: keep int main() working in GUI mode
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup)
endif()