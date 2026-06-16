#pragma once

#include "VulkanContext.hpp"
#include "scene/Node.hpp"
#include "scene/CameraNode.hpp"
#include "scene/MeshNode.hpp"
#include "renderer/StandardPipeline.hpp"
#include "renderer/CullPipeline.hpp"

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
    std::unique_ptr<CullPipeline> cullPipeline;

    // Descriptor resources for compute
    VkDescriptorPool computeDescriptorPool = VK_NULL_HANDLE;

    // Scene Graph
    std::unique_ptr<Node> rootNode;
    CameraNode* cameraNode = nullptr;

    // GPU representation of loaded meshes
    struct GPUMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;
        
        // Original CPU-uploaded MDI buffer (used as compute input)
        VkBuffer indirectBuffer = VK_NULL_HANDLE;
        VmaAllocation indirectAllocation = VK_NULL_HANDLE;
        
        // Double-buffered dynamic culled MDI buffers (compute output, rasterizer input)
        VkBuffer culledIndirectBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VmaAllocation culledIndirectAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        
        // Double-buffered draw counts (compute output, MDI count input)
        VkBuffer drawCountBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VmaAllocation drawCountAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        
        // Static meshlet bounding sphere and cone data
        VkBuffer boundsBuffer = VK_NULL_HANDLE;
        VmaAllocation boundsAllocation = VK_NULL_HANDLE;
        
        // Compute descriptor sets (one per frame in flight)
        VkDescriptorSet computeDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

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

    // Visual verification of culling (Freeze Frustum)
    bool freezeCulling = false;
    bool fKeyWasPressed = false;
    glm::vec4 frozenFrustumPlanes[6];
    glm::vec3 frozenCameraPos;

    void uploadMesh(const MeshNode& meshNode, GPUMesh& gpuMesh);
    void createComputeDescriptorSets(GPUMesh& gpuMesh);

    // Frame timing
    float lastFrameTime = 0.0f;
    bool framebufferResized = false;
};
