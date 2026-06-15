cmake_minimum_required(VERSION 3.20)
project(filigree VERSION 0.1.0 LANGUAGES CXX)

# Require C++20 for modern features (concepts, modules support, etc.)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 1. Find Vulkan SDK
# This searches for the LunarG Vulkan SDK environment variables to locate headers and libraries.
find_package(Vulkan REQUIRED)

# 2. Find dependencies managed by vcpkg
# vcpkg automatically hooks find_package for glfw3 and glm.
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# 3. Collect source files
# It's cleaner to explicitly list files rather than globbing, so CMake knows when files are added/removed.
set(SOURCES
    src/main.cpp
    src/Application.hpp
    src/Application.cpp
)

# 4. Define our executable target
add_executable(filigree ${SOURCES})

# 5. Set up includes
# Include our 'src' directory so we can do #include "Application.hpp" instead of relative paths.
target_include_directories(filigree PRIVATE
    src
)

# 6. Link dependencies
# Vulkan::Vulkan is the imported target provided by find_package(Vulkan)
# glfw is provided by glfw3
# glm::glm is provided by glm
target_link_libraries(filigree PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# Configure output directories to keep the build directory tidy
set_target_properties(filigree PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
