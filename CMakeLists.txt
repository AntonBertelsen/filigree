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
find_package(meshoptimizer CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)


# 3. Collect source files
# It's cleaner to explicitly list files rather than globbing, so CMake knows when files are added/removed.
set(SOURCES
    src/main.cpp
    src/core/VulkanContext.hpp
    src/core/VulkanContext.cpp
    src/core/Engine.hpp
    src/core/Engine.cpp
    src/ui/DebugUI.hpp
    src/ui/DebugUI.cpp
    src/scene/Node.hpp
    src/scene/Node.cpp
    src/scene/CameraNode.hpp
    src/scene/CameraNode.cpp
    src/scene/MeshNode.hpp
    src/scene/MeshNode.cpp
    src/renderer/pipelines/StandardPipeline.hpp
    src/renderer/pipelines/StandardPipeline.cpp
    src/renderer/pipelines/VisBufferPipeline.hpp
    src/renderer/pipelines/VisBufferPipeline.cpp
    src/renderer/pipelines/ResolvePipeline.hpp
    src/renderer/pipelines/ResolvePipeline.cpp
    src/renderer/pipelines/CullPipeline.hpp
    src/renderer/pipelines/CullPipeline.cpp
    src/geometry/ClusterDAGBuilder.hpp
    src/geometry/ClusterDAGBuilder.cpp
    src/geometry/GPUMesh.hpp
    src/geometry/GPUMeshUploader.hpp
    src/geometry/GPUMeshUploader.cpp
    src/core/InputController.hpp
    src/core/InputController.cpp
    src/renderer/VulkanRenderer.hpp
    src/renderer/VulkanRenderer.cpp
    src/renderer/passes/IRenderPass.hpp
    src/renderer/passes/CullComputePass.hpp
    src/renderer/passes/CullComputePass.cpp
    src/renderer/passes/HzbDownsamplePass.hpp
    src/renderer/passes/HzbDownsamplePass.cpp
    src/renderer/passes/VisBufferPass.hpp
    src/renderer/passes/VisBufferPass.cpp
    src/renderer/passes/ResolvePass.hpp
    src/renderer/passes/ResolvePass.cpp
    src/renderer/passes/ForwardPass.hpp
    src/renderer/passes/ForwardPass.cpp
    src/renderer/passes/DebugOverlayPass.hpp
    src/renderer/passes/DebugOverlayPass.cpp
    src/renderer/pipelines/HzbPipeline.hpp
    src/renderer/pipelines/HzbPipeline.cpp
    src/renderer/pipelines/DebugPipeline.hpp
    src/renderer/pipelines/DebugPipeline.cpp
    src/renderer/pipelines/BoundsPipeline.hpp
    src/renderer/pipelines/BoundsPipeline.cpp
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
    meshoptimizer::meshoptimizer
    imgui::imgui
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
    "${CMAKE_BINARY_DIR}/bin/shaders/cull.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/hzb.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/debug_vert.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/debug_frag.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/bounds_vert.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/bounds_frag.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_vert.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_frag.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_earlyz_frag.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/resolve_vert.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/resolve_frag.spv"
    "${CMAKE_BINARY_DIR}/bin/shaders/rasterize.spv"
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

# Custom command to compile compute shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/cull.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cull.comp" -o "${CMAKE_BINARY_DIR}/bin/shaders/cull.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cull.comp"
    COMMENT "Compiling compute shader to SPIR-V..."
)

# Custom command to compile hzb compute shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/hzb.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/hzb.comp" -o "${CMAKE_BINARY_DIR}/bin/shaders/hzb.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/hzb.comp"
    COMMENT "Compiling HZB compute shader to SPIR-V..."
)

# Custom command to compile debug vertex shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/debug_vert.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug.vert" -o "${CMAKE_BINARY_DIR}/bin/shaders/debug_vert.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug.vert"
    COMMENT "Compiling debug vertex shader to SPIR-V..."
)

# Custom command to compile debug fragment shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/debug_frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug.frag" -o "${CMAKE_BINARY_DIR}/bin/shaders/debug_frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug.frag"
    COMMENT "Compiling debug fragment shader to SPIR-V..."
)

# Custom command to compile bounds vertex shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/bounds_vert.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounds.vert" -o "${CMAKE_BINARY_DIR}/bin/shaders/bounds_vert.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounds.vert"
    COMMENT "Compiling bounds vertex shader to SPIR-V..."
)

# Custom command to compile bounds fragment shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/bounds_frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounds.frag" -o "${CMAKE_BINARY_DIR}/bin/shaders/bounds_frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bounds.frag"
    COMMENT "Compiling bounds fragment shader to SPIR-V..."
)

# Custom command to compile visbuffer vertex shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_vert.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.vert" -o "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_vert.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.vert"
    COMMENT "Compiling VisBuffer vertex shader to SPIR-V..."
)

# Custom command to compile visbuffer fragment shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.frag" -o "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.frag"
    COMMENT "Compiling VisBuffer fragment shader (no early-Z) to SPIR-V..."
)

# Custom command to compile visbuffer fragment shader with early-Z
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_earlyz_frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.frag" -DEARLY_Z=1 -o "${CMAKE_BINARY_DIR}/bin/shaders/visbuffer_earlyz_frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/visbuffer.frag"
    COMMENT "Compiling VisBuffer fragment shader (with early-Z) to SPIR-V..."
)

# Custom command to compile resolve vertex shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/resolve_vert.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/resolve.vert" -o "${CMAKE_BINARY_DIR}/bin/shaders/resolve_vert.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/resolve.vert"
    COMMENT "Compiling Resolve vertex shader to SPIR-V..."
)

# Custom command to compile resolve fragment shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/resolve_frag.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/resolve.frag" -o "${CMAKE_BINARY_DIR}/bin/shaders/resolve_frag.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/resolve.frag"
    COMMENT "Compiling Resolve fragment shader to SPIR-V..."
)

# Custom command to compile software rasterizer compute shader
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/bin/shaders/rasterize.spv"
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/rasterize.comp" -o "${CMAKE_BINARY_DIR}/bin/shaders/rasterize.spv"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/rasterize.comp"
    COMMENT "Compiling Software Rasterizer compute shader to SPIR-V..."
)

# Create a target for shaders that our main executable depends on
add_custom_target(compile_shaders DEPENDS ${SHADER_SPV_OUTPUTS})
add_dependencies(filigree compile_shaders)

# Define preprocessor macro pointing to the compiled shaders directory for C++ code
target_compile_definitions(filigree PRIVATE SHADERS_DIR="${CMAKE_BINARY_DIR}/bin/shaders/")
