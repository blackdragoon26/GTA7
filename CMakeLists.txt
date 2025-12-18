cmake_minimum_required(VERSION 3.15)
project(GTA7 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Fetch Dependencies ---
include(FetchContent)

# ---- GLFW ----
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ---- GLM (header-only) ----
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1  # Stable release
)
FetchContent_MakeAvailable(glm)

# --- Pre-generated GLAD ---
add_library(glad STATIC glad/src/gl.c)
target_include_directories(glad PUBLIC glad/include)

# --- Executable ---
add_executable(GTA7 main.cpp)

# Include directories for all dependencies
target_include_directories(GTA7 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${glm_SOURCE_DIR}        # ← GLM headers
)

target_link_libraries(GTA7 
    glad
    glfw
)

# --- Audio ---
configure_file(enginesound.mp3 ${CMAKE_CURRENT_BINARY_DIR}/enginesound.mp3 COPYONLY)

# --- macOS frameworks ---
if(APPLE)
    target_link_options(GTA7 PRIVATE
        "SHELL:-framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL"
    )
endif()

message(STATUS "GTA7: Built with bundled GLFW, GLAD, and GLM")