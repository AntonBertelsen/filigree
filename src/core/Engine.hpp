#pragma once

#include "VulkanContext.hpp"
#include "scene/Node.hpp"
#include "scene/CameraNode.hpp"
#include "scene/MeshNode.hpp"
#include "renderer/StandardPipeline.hpp"

#include <memory>
#include <vector>
#include <string>


class Engine {
public:
    Engine();
    ~Engine();

    // Prevent copying
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

    void setFramebufferResized(bool resized) { framebufferResized = resized; }
    void recreateSwapChain();
    void handleWindowRefresh();

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

    // GPU representation of loaded meshes
    struct GPUMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;
        VkBuffer indirectBuffer = VK_NULL_HANDLE;
        VmaAllocation indirectAllocation = VK_NULL_HANDLE;
        uint32_t clusterCount = 0;
    };

    struct ModelAsset {
        std::string name;
        std::string path;
        glm::vec3 position;
        float scale;
        GPUMesh gpuMesh;
        MeshNode* sceneNode = nullptr;
    };

    std::vector<ModelAsset> models;
    uint32_t activeModelIndex = 0;
    bool tabWasPressed = false;

    void uploadMesh(const MeshNode& meshNode, GPUMesh& gpuMesh);

    // Frame timing
    float lastFrameTime = 0.0f;
    bool framebufferResized = false;
};
