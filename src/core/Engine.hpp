#pragma once

#include "VulkanContext.hpp"
#include "scene/Node.hpp"
#include "scene/CameraNode.hpp"
#include "scene/MeshNode.hpp"
#include "renderer/StandardPipeline.hpp"

#include <memory>

class Engine {
public:
    Engine();
    ~Engine();

    // Prevent copying
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void initWindow();
    void mainLoop();
    void cleanup();

    void drawFrame();
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);

    // Window configuration
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    GLFWwindow* window = nullptr;

    // Core modules
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<StandardPipeline> pipeline;

    // Scene Graph
    std::unique_ptr<Node> rootNode;
    CameraNode* cameraNode = nullptr;
    MeshNode* bunnyNode = nullptr;
    MeshNode* lucyNode = nullptr;
    MeshNode* torusNode = nullptr;

    // GPU representation of loaded meshes
    struct GPUMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    GPUMesh bunnyMesh;
    GPUMesh lucyMesh;
    GPUMesh torusMesh;

    enum class ModelType {
        Bunny,
        Lucy,
        TorusKnot
    };
    ModelType activeModel = ModelType::Bunny;
    bool tabWasPressed = false;

    void uploadMesh(const MeshNode& meshNode, GPUMesh& gpuMesh);

    // Frame timing
    float lastFrameTime = 0.0f;
};
