#include "Engine.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "geometry/ClusterDAGBuilder.hpp"
#include "geometry/GPUMeshUploader.hpp"
#include "core/InputController.hpp"
#include "renderer/pipelines/HzbPipeline.hpp"
#include "renderer/pipelines/DebugPipeline.hpp"

#include <iostream>
#include <stdexcept>
#include <array>

Engine::Engine() {
    initWindow();
    context = std::make_unique<VulkanContext>(window);
    pipeline = std::make_unique<StandardPipeline>(*context);
    cullPipeline = std::make_unique<CullPipeline>(*context);
    hzbPipeline = std::make_unique<HzbPipeline>(*context);
    debugPipeline = std::make_unique<DebugPipeline>(*context);
    renderer = std::make_unique<VulkanRenderer>(*context, *pipeline, *cullPipeline);
    
    rootNode = std::make_unique<Node>();
    
    auto camera = std::make_unique<CameraNode>();
    cameraNode = camera.get();
    rootNode->addChild(std::move(camera));

    inputController = std::make_unique<InputController>(window, cameraNode);

    // Allocate compute descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 24; // 4 bindings * 2 frames in flight * 3 models
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 6; // 1 binding * 2 frames in flight * 3 models

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 8;

    if (vkCreateDescriptorPool(context->getDevice(), &poolInfo, nullptr, &computeDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor pool!");
    }
    
    // Asset Configuration list
    struct AssetConfig {
        std::string name;
        std::string path;
        glm::vec3 position;
        float scale;
    };
    std::vector<AssetConfig> configs = {
        {"Bunny", "assets/models/bunny.obj", {0.0f, -0.7f, 0.0f}, 1.0f},
        {"Lucy", "assets/models/lucy.obj", {0.0f, -0.8f, 0.0f}, 0.002f},
        {"Torus Knot", "assets/models/torus_knot.obj", {0.0f, 0.0f, 0.0f}, 0.25f}
    };
    
    for (const auto& config : configs) {
        std::cout << "Loading " << config.name << " model..." << std::endl;
        auto meshNode = std::make_unique<MeshNode>(config.path);
        meshNode->setPosition(config.position);
        meshNode->setScale(glm::vec3(config.scale));
        
        MeshNode* weakNode = meshNode.get();
        rootNode->addChild(std::move(meshNode));
        
        ModelAsset asset{};
        asset.name = config.name;
        asset.path = config.path;
        asset.position = config.position;
        asset.scale = config.scale;
        asset.sceneNode = weakNode;
        
        std::cout << "Building Cluster DAG & uploading " << config.name << " to GPU..." << std::endl;
        MeshletData meshletData = ClusterDAGBuilder::buildClusterDAG(weakNode->getVertices(), weakNode->getIndices());
        
        GPUMeshUploader::uploadMesh(
            *context, 
            computeDescriptorPool, 
            cullPipeline->getDescriptorSetLayout(), 
            meshletData, 
            asset.gpuMesh
        );
        
        models.push_back(asset);
    }
    
    lastFrameTime = static_cast<float>(glfwGetTime());
}

Engine::~Engine() {
    cleanup();
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (engine) {
        engine->setFramebufferResized(true);
        if (width > 0 && height > 0) {
            engine->handleWindowRefresh();
        }
    }
}

static void windowRefreshCallback(GLFWwindow* window) {
    auto engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (engine) {
        engine->handleWindowRefresh();
    }
}

void Engine::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Filigree Engine", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetWindowRefreshCallback(window, windowRefreshCallback);
}

void Engine::run() {
    mainLoop();
}

void Engine::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Calculate delta time
        float currentFrameTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        // Poll Input
        inputController->update(deltaTime);
        
        if (inputController->isTabPressedThisFrame()) {
            renderModeNanite = !renderModeNanite;
            std::cout << "[Mode Switch] Render mode changed to: " << (renderModeNanite ? "NANITE" : "TRADITIONAL") << std::endl;
        }

        if (inputController->isLPressedThisFrame()) {
            lodEnabled = !lodEnabled;
            std::cout << "[LOD Selection] LOD system: " << (lodEnabled ? "ENABLED" : "DISABLED") << std::endl;
        }

        if (inputController->is1PressedThisFrame()) {
            lodThreshold = std::max(0.1f, lodThreshold - 0.5f);
            std::cout << "[LOD Selection] LOD screen threshold decreased to: " << lodThreshold << " pixels" << std::endl;
        }

        if (inputController->is2PressedThisFrame()) {
            lodThreshold += 0.5f;
            std::cout << "[LOD Selection] LOD screen threshold increased to: " << lodThreshold << " pixels" << std::endl;
        }

        if (inputController->isMPressedThisFrame()) {
            activeModelIndex = (activeModelIndex + 1) % models.size();
            std::cout << "[Asset Switch] Active model cycled to: " << models[activeModelIndex].name << std::endl;
        }

        if (inputController->isHPressedThisFrame()) {
            hzbCullingEnabled = !hzbCullingEnabled;
            std::cout << "[HZB Culling] Occlusion culling: " << (hzbCullingEnabled ? "ENABLED" : "DISABLED") << std::endl;
        }

        if (inputController->isVPressedThisFrame()) {
            debugVisualiseHzb = !debugVisualiseHzb;
            std::cout << "[HZB Visualizer] Debug view: " << (debugVisualiseHzb ? "ENABLED" : "DISABLED") << std::endl;
        }

        if (inputController->isUpPressedThisFrame()) {
            if (debugHzbMipLevel < 10) {
                debugHzbMipLevel++;
                std::cout << "[HZB Visualizer] Mip level incremented to: " << debugHzbMipLevel << " (" << std::max(1u, 1024u >> debugHzbMipLevel) << "x" << std::max(1u, 1024u >> debugHzbMipLevel) << ")" << std::endl;
            }
        }

        if (inputController->isDownPressedThisFrame()) {
            if (debugHzbMipLevel > 0) {
                debugHzbMipLevel--;
                std::cout << "[HZB Visualizer] Mip level decremented to: " << debugHzbMipLevel << " (" << std::max(1u, 1024u >> debugHzbMipLevel) << "x" << std::max(1u, 1024u >> debugHzbMipLevel) << ")" << std::endl;
            }
        }

        if (inputController->isFPressedThisFrame()) {
            freezeCulling = !freezeCulling;
            std::cout << "[Culling State] Freeze culling: " << (freezeCulling ? "ENABLED" : "DISABLED") << std::endl;
        }

        if (inputController->isBPressedThisFrame()) {
            drawBoundingSpheres = !drawBoundingSpheres;
            std::cout << "[Debug Bounds] Bounding spheres visualization: " << (drawBoundingSpheres ? "ENABLED" : "DISABLED") << std::endl;
        }

        if (inputController->is3PressedThisFrame()) {
            visBufferDebugMode = (visBufferDebugMode + 1) % 5;
            std::vector<std::string> modes = { "SHADED", "NEUTRAL", "TRIANGLE_ID", "BARYCENTRICS", "MESHLET_ID" };
            std::cout << "[VisBuffer Debug] Mode changed to: " << modes[visBufferDebugMode] << std::endl;
        }

        // Print telemetry once every 60 frames
        telemetryFrameCount++;
        if (telemetryFrameCount >= 60) {
            telemetryFrameCount = 0;
            if (activeModelIndex < models.size()) {
                const auto& activeAsset = models[activeModelIndex];
                uint32_t drawCount = 0;
                uint32_t currentFrame = context->getCurrentFrameIndex();
                
                void* mappedData;
                vmaMapMemory(context->getAllocator(), activeAsset.gpuMesh.drawCountAllocation[currentFrame], &mappedData);
                drawCount = *static_cast<uint32_t*>(mappedData);
                vmaUnmapMemory(context->getAllocator(), activeAsset.gpuMesh.drawCountAllocation[currentFrame]);
                
                std::cout << "[Telemetry] Render Mode: " << (renderModeNanite ? "NANITE" : "TRADITIONAL")
                          << " | Model: " << activeAsset.name
                          << " | Clusters Drawn: " << (renderModeNanite ? std::to_string(drawCount) : "N/A (All)")
                          << " / " << activeAsset.gpuMesh.clusterCount
                          << " | LOD: " << (lodEnabled ? "ON (" + std::to_string(lodThreshold) + "px)" : "OFF")
                          << " | HZB Culling: " << (hzbCullingEnabled ? "ON" : "OFF")
                          << std::endl;
            }
        }

        // Update Scene Graph
        rootNode->update(deltaTime);
        rootNode->updateWorldMatrix(glm::mat4(1.0f));

        // Draw
        renderer->drawFrame(*this);
    }

    // Wait for GPU to finish work before exiting
    vkDeviceWaitIdle(context->getDevice());
}

void Engine::cleanup() {
    // Release scene graph before Vulkan cleanup
    rootNode.reset();
    cameraNode = nullptr;

    // Release GPU Mesh buffers
    if (context) {
        VmaAllocator allocator = context->getAllocator();
        for (auto& asset : models) {
            if (asset.gpuMesh.vertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.vertexBuffer, asset.gpuMesh.vertexAllocation);
                asset.gpuMesh.vertexBuffer = VK_NULL_HANDLE;
            }
            if (asset.gpuMesh.indexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.indexBuffer, asset.gpuMesh.indexAllocation);
                asset.gpuMesh.indexBuffer = VK_NULL_HANDLE;
            }
            if (asset.gpuMesh.traditionalVertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.traditionalVertexBuffer, asset.gpuMesh.traditionalVertexAllocation);
                asset.gpuMesh.traditionalVertexBuffer = VK_NULL_HANDLE;
            }
            if (asset.gpuMesh.traditionalIndexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.traditionalIndexBuffer, asset.gpuMesh.traditionalIndexAllocation);
                asset.gpuMesh.traditionalIndexBuffer = VK_NULL_HANDLE;
            }
            if (asset.gpuMesh.indirectBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.indirectBuffer, asset.gpuMesh.indirectAllocation);
                asset.gpuMesh.indirectBuffer = VK_NULL_HANDLE;
            }
            for (int i = 0; i < 2; ++i) {
                if (asset.gpuMesh.culledIndirectBuffer[i] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, asset.gpuMesh.culledIndirectBuffer[i], asset.gpuMesh.culledIndirectAllocation[i]);
                    asset.gpuMesh.culledIndirectBuffer[i] = VK_NULL_HANDLE;
                    asset.gpuMesh.culledIndirectAllocation[i] = VK_NULL_HANDLE;
                }
                if (asset.gpuMesh.drawCountBuffer[i] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, asset.gpuMesh.drawCountBuffer[i], asset.gpuMesh.drawCountAllocation[i]);
                    asset.gpuMesh.drawCountBuffer[i] = VK_NULL_HANDLE;
                    asset.gpuMesh.drawCountAllocation[i] = VK_NULL_HANDLE;
                }
                if (asset.gpuMesh.visibilityBuffer[i] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, asset.gpuMesh.visibilityBuffer[i], asset.gpuMesh.visibilityAllocation[i]);
                    asset.gpuMesh.visibilityBuffer[i] = VK_NULL_HANDLE;
                    asset.gpuMesh.visibilityAllocation[i] = VK_NULL_HANDLE;
                }
            }
            if (asset.gpuMesh.boundsBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.boundsBuffer, asset.gpuMesh.boundsAllocation);
                asset.gpuMesh.boundsBuffer = VK_NULL_HANDLE;
                asset.gpuMesh.boundsAllocation = VK_NULL_HANDLE;
            }
            if (asset.gpuMesh.hzbSampler != VK_NULL_HANDLE) {
                vkDestroySampler(context->getDevice(), asset.gpuMesh.hzbSampler, nullptr);
                asset.gpuMesh.hzbSampler = VK_NULL_HANDLE;
            }
        }
    }
    models.clear();

    // Release Descriptor Pools
    if (computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context->getDevice(), computeDescriptorPool, nullptr);
        computeDescriptorPool = VK_NULL_HANDLE;
    }

    // Release pipelines, sub-systems and context
    inputController.reset();
    renderer.reset();
    debugPipeline.reset();
    hzbPipeline.reset();
    cullPipeline.reset();
    pipeline.reset();
    context.reset();

    // Cleanup window
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

void Engine::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(context->getDevice());
    context->recreateSwapChain();

    if (hzbPipeline) {
        hzbPipeline->updateDescriptorSets();
    }
    if (debugPipeline) {
        debugPipeline->updateDescriptorSets();
    }

    if (renderer) {
        renderer->recreateVisBuffer();
    }

    for (auto& asset : models) {
        GPUMeshUploader::updateDescriptorSets(*context, asset.gpuMesh);
    }
}

void Engine::handleWindowRefresh() {
    // Calculate delta time
    float currentFrameTime = static_cast<float>(glfwGetTime());
    float deltaTime = currentFrameTime - lastFrameTime;
    lastFrameTime = currentFrameTime;

    // Update Input, Camera and Scene Graph
    inputController->update(deltaTime);
    rootNode->update(deltaTime);
    rootNode->updateWorldMatrix(glm::mat4(1.0f));

    renderer->drawFrame(*this);
}
