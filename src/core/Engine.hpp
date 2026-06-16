#pragma once

#include "VulkanContext.hpp"
#include "scene/Node.hpp"
#include "scene/CameraNode.hpp"
#include "scene/MeshNode.hpp"
#include "renderer/StandardPipeline.hpp"
#include "renderer/CullPipeline.hpp"
#include "core/GPUMesh.hpp"

#include <memory>
#include <vector>
#include <string>

// Forward declarations
class VulkanRenderer;
class InputController;

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

    // Grant access to sub-systems
    friend class VulkanRenderer;
    friend class InputController;

private:
    void initWindow();
    void mainLoop();
    void cleanup();

    // Window configuration
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    GLFWwindow* window = nullptr;

    // Core modules
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<StandardPipeline> pipeline;
    std::unique_ptr<CullPipeline> cullPipeline;
    std::unique_ptr<VulkanRenderer> renderer;
    std::unique_ptr<InputController> inputController;

    // Descriptor resources for compute
    VkDescriptorPool computeDescriptorPool = VK_NULL_HANDLE;

    // Scene Graph
    std::unique_ptr<Node> rootNode;
    CameraNode* cameraNode = nullptr;

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

    // Visual verification of culling (Freeze Frustum)
    bool freezeCulling = false;
    glm::vec4 frozenFrustumPlanes[6];
    glm::vec3 frozenCameraPos;

    // Frame timing
    float lastFrameTime = 0.0f;
    bool framebufferResized = false;
};
