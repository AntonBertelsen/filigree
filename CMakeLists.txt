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
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)

# 3. Collect source files
# It's cleaner to explicitly list files rather than globbing, so CMake knows when files are added/removed.
set(SOURCES
    src/main.cpp
    src/core/VulkanContext.hpp
    src/core/VulkanContext.cpp
    src/core/Engine.hpp
    src/core/Engine.cpp
    src/scene/Node.hpp
    src/scene/Node.cpp
    src/scene/CameraNode.hpp
    src/scene/CameraNode.cpp
    src/scene/MeshNode.hpp
    src/scene/MeshNode.cpp
    src/renderer/StandardPipeline.hpp
    src/renderer/StandardPipeline.cpp
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
    GPUOpen::VulkanMemoryAllocator
    tinyobjloader::tinyobjloader
)

# Configure output directories to keep the build directory tidy
set_target_properties(filigree PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 7. Shader Compilation
# Find glslc compiler (part of the Vulkan SDK)
if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc shader compiler not found! Make sure the Vulkan SDK is installed.")
endif()

# We want the compiled shaders to end up next to our executable in the build directory.
set(SHADER_SPV_OUTPUTS
    "${CMAKE_BINARY_DIR}/bin/shaders/vert.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/frag.spv"
)

# Create output shader directory in the build folder
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/shaders")

# Custom command to compile vertex shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/vert.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert" -o "${CMAKE_BINARY_DIR}/bin/shaders/vert.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert"
    COMMENT "Compiling vertex shader to SPIR-V..."
)

# Custom command to compile fragment shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag" -o "${CMAKE_BINARY_DIR}/bin/shaders/frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag"
    COMMENT "Compiling fragment shader to SPIR-V..."
)

# Create a target for shaders that our main executable depends on
add_custom_target(compile_shaders DEPENDS ${SHADER_SPV_OUTPUTS})
add_dependencies(filigree compile_shaders)

# Define preprocessor macro pointing to the compiled shaders directory for C++ code
target_compile_definitions(filigree PRIVATE SHADERS_DIR="${CMAKE_BINARY_DIR}/bin/shaders/")
