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
#include <algorithm>

Engine::Engine() {
    initWindow();
    context = std::make_unique<VulkanContext>(window);
    cullPipeline = std::make_unique<CullPipeline>(*context);
    pipeline = std::make_unique<StandardPipeline>(*context, cullPipeline->getDescriptorSetLayout());
    hzbPipeline = std::make_unique<HzbPipeline>(*context);
    debugPipeline = std::make_unique<DebugPipeline>(*context);
    renderer = std::make_unique<VulkanRenderer>(*context, *pipeline, *cullPipeline, *hzbPipeline, *debugPipeline);
    
    rootNode = std::make_unique<Node>();
    
    auto camera = std::make_unique<CameraNode>();
    cameraNode = camera.get();
    rootNode->addChild(std::move(camera));

    inputController = std::make_unique<InputController>(window, cameraNode);

    // Allocate compute descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 96;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 12;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 16;

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
    
    std::vector<MeshletData> meshletDatas;
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
        
        std::cout << "Building Cluster DAG for " << config.name << "..." << std::endl;
        MeshletData meshletData = ClusterDAGBuilder::buildClusterDAG(weakNode->getVertices(), weakNode->getIndices());
        meshletDatas.push_back(meshletData);
        
        models.push_back(asset);
    }

    std::cout << "Uploading unified scene geometry to GPU..." << std::endl;
    std::vector<GPUMesh> gpuMeshes;
    GPUMeshUploader::uploadScene(
        *context,
        computeDescriptorPool,
        cullPipeline->getDescriptorSetLayout(),
        meshletDatas,
        gpuScene,
        gpuMeshes
    );

    for (size_t i = 0; i < models.size(); ++i) {
        models[i].gpuMesh = gpuMeshes[i];
    }

    // Propagate scene node transforms first to get correct world matrices
    rootNode->updateWorldMatrix(glm::mat4(1.0f));

    // Upload initial scene instances
    updateSceneInstances();
    
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
            geometryPipeline = (geometryPipeline == GeometryPipeline::NANITE) ? GeometryPipeline::TRADITIONAL : GeometryPipeline::NANITE;
            std::cout << "[Mode Switch] Geometry pipeline changed to: " << (geometryPipeline == GeometryPipeline::NANITE ? "NANITE" : "TRADITIONAL") << std::endl;
        }

        if (inputController->is4PressedThisFrame()) {
            shadingPath = (shadingPath == ShadingPath::DEFERRED) ? ShadingPath::FORWARD : ShadingPath::DEFERRED;
            std::cout << "[Shading Switch] Shading path changed to: " << (shadingPath == ShadingPath::DEFERRED ? "DEFERRED" : "FORWARD") << std::endl;
        }

        if (inputController->is5PressedThisFrame()) {
            rasterizerMode = static_cast<RasterizerMode>((static_cast<int>(rasterizerMode) + 1) % 3);
            std::vector<std::string> modes = { "PURE_HARDWARE", "PURE_SOFTWARE", "HYBRID" };
            std::cout << "[Rasterizer Mode] Mode changed to: " << modes[static_cast<int>(rasterizerMode)] << std::endl;
        }

        if (inputController->is6PressedThisFrame()) {
            hwPathMode = static_cast<HardwarePathMode>((static_cast<int>(hwPathMode) + 1) % 2);
            std::vector<std::string> hwModes = { "PURE_UAV", "DEPTH_TESTED" };
            std::cout << "[Hardware Path] Mode changed to: " << hwModes[static_cast<int>(hwPathMode)] << std::endl;
        }

        if (inputController->is7PressedThisFrame()) {
            syncMode = static_cast<SyncMode>((static_cast<int>(syncMode) + 1) % 2);
            std::vector<std::string> syncModes = { "SEQUENTIAL", "PARALLEL" };
            std::cout << "[Pipeline Sync] Mode changed to: " << syncModes[static_cast<int>(syncMode)] << std::endl;
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
            activeModelIndex = (activeModelIndex + 1) % 4;
            if (activeModelIndex < 3) {
                std::cout << "[Asset Switch] Active model cycled to: " << models[activeModelIndex].name << std::endl;
            } else {
                std::cout << "[Asset Switch] Active model cycled to: Scattered Forest (120 instances)" << std::endl;
            }
            updateSceneInstances();
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
            if (activeModelIndex < 4) {
                uint32_t totalDrawCount = 0;
                uint32_t totalSoftwareDrawCount = 0;
                uint32_t totalClusterCount = 0;
                uint32_t currentFrame = context->getCurrentFrameIndex();
                bool isNanite = (geometryPipeline == GeometryPipeline::NANITE);

                totalClusterCount = gpuScene.totalCullTasks;
                if (isNanite) {
                    if (gpuScene.drawCountBuffer[currentFrame] != VK_NULL_HANDLE) {
                        void* mappedData = nullptr;
                        vmaMapMemory(context->getAllocator(), gpuScene.drawCountAllocation[currentFrame], &mappedData);
                        totalDrawCount = *static_cast<uint32_t*>(mappedData);
                        vmaUnmapMemory(context->getAllocator(), gpuScene.drawCountAllocation[currentFrame]);
                    }
                    if (gpuScene.softwareDrawCountBuffer[currentFrame] != VK_NULL_HANDLE) {
                        void* mappedData = nullptr;
                        vmaMapMemory(context->getAllocator(), gpuScene.softwareDrawCountAllocation[currentFrame], &mappedData);
                        totalSoftwareDrawCount = *static_cast<uint32_t*>(mappedData);
                        vmaUnmapMemory(context->getAllocator(), gpuScene.softwareDrawCountAllocation[currentFrame]);
                    }
                }

                std::vector<std::string> rasterModes = { "PURE_HARDWARE", "PURE_SOFTWARE", "HYBRID" };
                std::string modeStr = isNanite ? rasterModes[static_cast<int>(rasterizerMode)] : "TRADITIONAL";

                std::vector<std::string> hwModes = { "PURE_UAV", "DEPTH_TESTED" };
                std::vector<std::string> syncModes = { "SEQUENTIAL", "PARALLEL" };

                std::string modelName = (activeModelIndex < 3) ? models[activeModelIndex].name : "Scattered Forest";
                std::cout << "[Telemetry] Geometry: " << (isNanite ? "NANITE" : "TRADITIONAL")
                          << " | Mode: " << modeStr
                          << " (HW: " << (isNanite ? hwModes[static_cast<int>(hwPathMode)] : "N/A")
                          << ", Sync: " << (isNanite ? syncModes[static_cast<int>(syncMode)] : "N/A") << ")"
                          << " | Model: " << modelName
                          << " | HW Clusters: " << (isNanite ? std::to_string(totalDrawCount) : "N/A")
                          << " | SW Clusters: " << (isNanite ? std::to_string(totalSoftwareDrawCount) : "N/A")
                          << " / " << totalClusterCount
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

    // Release GPU Scene buffers
    if (context) {
        VkDevice device = context->getDevice();
        VmaAllocator allocator = context->getAllocator();

        if (gpuScene.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuScene.vertexBuffer, gpuScene.vertexAllocation);
        }
        if (gpuScene.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuScene.indexBuffer, gpuScene.indexAllocation);
        }
        if (gpuScene.traditionalIndexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuScene.traditionalIndexBuffer, gpuScene.traditionalIndexAllocation);
        }
        if (gpuScene.boundsBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuScene.boundsBuffer, gpuScene.boundsAllocation);
        }
        if (gpuScene.inputCommandsBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuScene.inputCommandsBuffer, gpuScene.inputCommandsAllocation);
        }

        for (int i = 0; i < 2; ++i) {
            if (gpuScene.instanceBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.instanceBuffer[i], gpuScene.instanceAllocation[i]);
            }
            if (gpuScene.cullTasksBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.cullTasksBuffer[i], gpuScene.cullTasksAllocation[i]);
            }
            if (gpuScene.traditionalIndirectBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.traditionalIndirectBuffer[i], gpuScene.traditionalIndirectAllocation[i]);
            }
            if (gpuScene.culledIndirectBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.culledIndirectBuffer[i], gpuScene.culledIndirectAllocation[i]);
            }
            if (gpuScene.drawCountBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.drawCountBuffer[i], gpuScene.drawCountAllocation[i]);
            }
            if (gpuScene.culledSoftwareIndirectBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.culledSoftwareIndirectBuffer[i], gpuScene.culledSoftwareIndirectAllocation[i]);
            }
            if (gpuScene.softwareDrawCountBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.softwareDrawCountBuffer[i], gpuScene.softwareDrawCountAllocation[i]);
            }
            if (gpuScene.visibilityBuffer[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, gpuScene.visibilityBuffer[i], gpuScene.visibilityAllocation[i]);
            }
        }

        if (gpuScene.hzbSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, gpuScene.hzbSampler, nullptr);
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

    if (renderer) {
        renderer->recreateVisBuffer();
    }
    if (renderer) {
        renderer->updateHzbDescriptorSets();
    }
    if (debugPipeline && renderer) {
        debugPipeline->updateDescriptorSets(
            renderer->getHzbImageView(0),
            renderer->getHzbImageView(1)
        );
    }

    // Recreate/update descriptor sets with the new visBuffer image view
    updateSceneInstances();
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

void Engine::updateSceneInstances() {
    std::vector<std::vector<glm::mat4>> modelInstances(models.size());

    if (activeModelIndex < 3) {
        glm::mat4 baseTransform = models[activeModelIndex].sceneNode->getWorldMatrix();
        modelInstances[activeModelIndex].push_back(baseTransform);
    } else {
        int bunnyCount = 0;
        int lucyCount = 0;
        int torusCount = 0;

        int cols = 11;
        int rows = 11;
        float spacing = 2.2f;
        float startX = -((cols - 1) * spacing) / 2.0f;
        float startZ = -((rows - 1) * spacing) / 2.0f;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (bunnyCount + lucyCount + torusCount >= 120) break;
                
                float x = startX + c * spacing;
                float z = startZ + r * spacing;
                
                float offsetX = (static_cast<float>((r * 7 + c * 13) % 10) / 10.0f - 0.5f) * 0.5f;
                float offsetZ = (static_cast<float>((r * 13 + c * 7) % 10) / 10.0f - 0.5f) * 0.5f;
                
                glm::vec3 pos = glm::vec3(x + offsetX, 0.0f, z + offsetZ);
                
                int modelType = (bunnyCount + lucyCount + torusCount) % 3;
                
                if (modelType == 0 && bunnyCount >= 40) {
                    modelType = (lucyCount < 40) ? 1 : 2;
                }
                if (modelType == 1 && lucyCount >= 40) {
                    modelType = (torusCount < 40) ? 2 : 0;
                }
                if (modelType == 2 && torusCount >= 40) {
                    modelType = (bunnyCount < 40) ? 0 : 1;
                }
                
                glm::mat4 mat = glm::mat4(1.0f);
                
                if (modelType == 0) {
                    pos.y = -0.7f;
                    mat = glm::translate(mat, pos);
                    mat = glm::scale(mat, glm::vec3(1.0f));
                    modelInstances[0].push_back(mat);
                    bunnyCount++;
                } else if (modelType == 1) {
                    pos.y = -0.8f;
                    mat = glm::translate(mat, pos);
                    mat = glm::scale(mat, glm::vec3(0.002f));
                    modelInstances[1].push_back(mat);
                    lucyCount++;
                } else {
                    pos.y = 0.0f;
                    mat = glm::translate(mat, pos);
                    mat = glm::scale(mat, glm::vec3(0.25f));
                    modelInstances[2].push_back(mat);
                    torusCount++;
                }
            }
        }
    }

    std::vector<GPUMesh> gpuMeshes(models.size());
    for (size_t i = 0; i < models.size(); ++i) {
        gpuMeshes[i] = models[i].gpuMesh;
    }

    GPUMeshUploader::uploadSceneInstances(*context, gpuScene, gpuMeshes, modelInstances);

    GPUMeshUploader::updateDescriptorSets(
        *context,
        gpuScene,
        renderer->getHzbImageView(0),
        renderer->getHzbImageView(1),
        renderer->getVisBufferSSBO()
    );
}
