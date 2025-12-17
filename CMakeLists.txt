cmake_minimum_required(VERSION 3.15)
project(InfiniteOpenWorld LANGUAGES CXX)

# Require C++17 (for std::filesystem, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Find dependencies ---
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# --- Create executable ---
add_executable(${PROJECT_NAME} main.cpp)

# --- Include directories ---
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- Link libraries ---
target_link_libraries(${PROJECT_NAME} 
    OpenGL::GL 
    glfw
)

# --- Copy audio to build directory ---
configure_file(enginesound.mp3 ${CMAKE_CURRENT_BINARY_DIR}/enginesound.mp3 COPYONLY)

# --- Platform-specific settings ---
if(APPLE)
    # Required for macOS OpenGL
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_VISIBILITY_PRESET hidden)
    set_property(TARGET ${PROJECT_NAME} PROPERTY VISIBILITY_PRESET hidden)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
elseif(WIN32)
    # Optional: embed manifest or set subsystem
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:CONSOLE")
endif()

# --- Optional: Enable MP3 support info ---
message(STATUS "Building InfiniteOpenWorld with miniaudio (MP3 enabled)")