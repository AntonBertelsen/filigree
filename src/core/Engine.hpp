#pragma once

#include "VulkanContext.hpp"
#include "scene/Node.hpp"
#include "scene/CameraNode.hpp"
#include "scene/MeshNode.hpp"
#include "renderer/pipelines/StandardPipeline.hpp"
#include "renderer/pipelines/CullPipeline.hpp"
#include "geometry/GPUMesh.hpp"

#include <memory>
#include <vector>
#include <string>

// Forward declarations
class VulkanRenderer;
class InputController;
class HzbPipeline;
class DebugPipeline;
class CullComputePass;
class HzbDownsamplePass;
class VisBufferPass;
class ResolvePass;
class ForwardPass;
class DebugOverlayPass;

class Engine {
public:
    enum class GeometryPipeline {
        TRADITIONAL,
        NANITE
    };

    enum class ShadingPath {
        FORWARD,
        DEFERRED
    };

    enum class RasterizerMode {
        PURE_HARDWARE = 0,
        PURE_SOFTWARE = 1,
        HYBRID = 2
    };

    enum class HardwarePathMode {
        PURE_UAV = 0,
        DEPTH_TESTED = 1
    };

    enum class SyncMode {
        SEQUENTIAL = 0,
        PARALLEL = 1
    };

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
    friend class CullComputePass;
    friend class HzbDownsamplePass;
    friend class VisBufferPass;
    friend class ResolvePass;
    friend class ForwardPass;
    friend class DebugOverlayPass;

private:
    void initWindow();
    void mainLoop();
    void cleanup();
    void updateSceneInstances();

    // Window configuration
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    GLFWwindow* window = nullptr;

    // Core modules
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<StandardPipeline> pipeline;
    std::unique_ptr<CullPipeline> cullPipeline;
    std::unique_ptr<HzbPipeline> hzbPipeline;
    std::unique_ptr<DebugPipeline> debugPipeline;
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
    GPUScene gpuScene;
    uint32_t activeModelIndex = 0;

    // Toggles and debug configurations
    GeometryPipeline geometryPipeline = GeometryPipeline::NANITE;
    ShadingPath shadingPath = ShadingPath::DEFERRED;
    RasterizerMode rasterizerMode = RasterizerMode::HYBRID;
    HardwarePathMode hwPathMode = HardwarePathMode::DEPTH_TESTED;
    SyncMode syncMode = SyncMode::SEQUENTIAL;
    bool hzbCullingEnabled = true;
    bool debugVisualiseHzb = false;
    uint32_t debugHzbMipLevel = 0;
    bool drawBoundingSpheres = false;
    bool lodEnabled = true;
    float lodThreshold = 2.0f; // in pixels
    uint32_t visBufferDebugMode = 0; // 0 = Shaded, 1 = Neutral, 2 = Triangle ID, 3 = Barycentrics, 4 = Meshlet ID

    // Visual verification of culling (Freeze Frustum)
    bool freezeCulling = false;
    glm::vec4 frozenFrustumPlanes[6];
    glm::vec3 frozenCameraPos;

    // Frame timing and telemetry
    float lastFrameTime = 0.0f;
    bool framebufferResized = false;
    uint32_t telemetryFrameCount = 0;
};
