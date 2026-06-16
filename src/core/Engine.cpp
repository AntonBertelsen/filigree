#include "Engine.hpp"

#include <iostream>
#include <stdexcept>
#include <chrono>
#include <array>
#include <meshoptimizer.h>

Engine::Engine() {
    initWindow();
    context = std::make_unique<VulkanContext>(window);
    pipeline = std::make_unique<StandardPipeline>(*context);
    cullPipeline = std::make_unique<CullPipeline>(*context);
    
    rootNode = std::make_unique<Node>();
    
    auto camera = std::make_unique<CameraNode>();
    cameraNode = camera.get();
    rootNode->addChild(std::move(camera));

    // Allocate compute descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 24; // 4 bindings * 2 frames in flight * 3 models

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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
        
        std::cout << "Uploading " << config.name << " mesh to GPU..." << std::endl;
        uploadMesh(*weakNode, asset.gpuMesh);
        createComputeDescriptorSets(asset.gpuMesh);
        
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

        // Poll Tab key to toggle between models
        bool currentTabState = (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS);
        if (currentTabState && !tabWasPressed && !models.empty()) {
            activeModelIndex = (activeModelIndex + 1) % models.size();
            std::cout << "Active model switched to: " << models[activeModelIndex].name << std::endl;
        }
        tabWasPressed = currentTabState;

        // Poll F key to freeze culling frustum
        bool currentFState = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
        if (currentFState && !fKeyWasPressed) {
            freezeCulling = !freezeCulling;
            std::cout << "[Culling State] Freeze culling: " << (freezeCulling ? "ENABLED" : "DISABLED") << std::endl;
        }
        fKeyWasPressed = currentFState;

        // Update Camera and Scene Graph
        // Note: cameraNode is updated directly since it polls input
        cameraNode->update(deltaTime, window);
        rootNode->update(deltaTime);
        rootNode->updateWorldMatrix(glm::mat4(1.0f));

        drawFrame();
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
            }
            if (asset.gpuMesh.boundsBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, asset.gpuMesh.boundsBuffer, asset.gpuMesh.boundsAllocation);
                asset.gpuMesh.boundsBuffer = VK_NULL_HANDLE;
                asset.gpuMesh.boundsAllocation = VK_NULL_HANDLE;
            }
        }
    }
    models.clear();

    // Release Descriptor Pools
    if (computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context->getDevice(), computeDescriptorPool, nullptr);
        computeDescriptorPool = VK_NULL_HANDLE;
    }

    // Release pipelines and context
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

void Engine::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    // 1. Transition swapchain image and depth image layouts
    VkImageMemoryBarrier2 barriers[2]{};
    
    // Color Barrier
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image = context->getSwapChainImages()[imageIndex];
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;

    // Depth Barrier
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].image = context->getDepthImage();
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 2;
    dependencyInfo.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cb, &dependencyInfo);

    // 2. GPU Compute Pass for Culling (executed outside of rendering pass)
    float aspect = static_cast<float>(context->getSwapChainExtent().width) / static_cast<float>(context->getSwapChainExtent().height);
    glm::mat4 viewProj = cameraNode->getProjectionMatrix(aspect) * cameraNode->getViewMatrix();
    
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    VkBuffer activeVertexBuffer = VK_NULL_HANDLE;
    VkBuffer activeIndexBuffer = VK_NULL_HANDLE;
    uint32_t activeClusterCount = 0;
    
    uint32_t currentFrame = context->getCurrentFrameIndex();
    const ModelAsset* activeAsset = nullptr;

    if (activeModelIndex < models.size()) {
        activeAsset = &models[activeModelIndex];
        modelMatrix = activeAsset->sceneNode->getWorldMatrix();
        activeVertexBuffer = activeAsset->gpuMesh.vertexBuffer;
        activeIndexBuffer = activeAsset->gpuMesh.indexBuffer;
        activeClusterCount = activeAsset->gpuMesh.clusterCount;
    }

    if (activeAsset) {
        const auto& mesh = activeAsset->gpuMesh;

        // Extract Frustum Planes in Model Space
        glm::mat4 modelViewProj = viewProj * modelMatrix;
        glm::vec4 planes[6];
        // Left
        planes[0] = glm::vec4(modelViewProj[0][3] + modelViewProj[0][0], modelViewProj[1][3] + modelViewProj[1][0], modelViewProj[2][3] + modelViewProj[2][0], modelViewProj[3][3] + modelViewProj[3][0]);
        // Right
        planes[1] = glm::vec4(modelViewProj[0][3] - modelViewProj[0][0], modelViewProj[1][3] - modelViewProj[1][0], modelViewProj[2][3] - modelViewProj[2][0], modelViewProj[3][3] - modelViewProj[3][0]);
        // Bottom
        planes[2] = glm::vec4(modelViewProj[0][3] + modelViewProj[0][1], modelViewProj[1][3] + modelViewProj[1][1], modelViewProj[2][3] + modelViewProj[2][1], modelViewProj[3][3] + modelViewProj[3][1]);
        // Top
        planes[3] = glm::vec4(modelViewProj[0][3] - modelViewProj[0][1], modelViewProj[1][3] - modelViewProj[1][1], modelViewProj[2][3] - modelViewProj[2][1], modelViewProj[3][3] - modelViewProj[3][1]);
        // Near
        planes[4] = glm::vec4(modelViewProj[0][2], modelViewProj[1][2], modelViewProj[2][2], modelViewProj[3][2]);
        // Far
        planes[5] = glm::vec4(modelViewProj[0][3] - modelViewProj[0][2], modelViewProj[1][3] - modelViewProj[1][2], modelViewProj[2][3] - modelViewProj[2][2], modelViewProj[3][3] - modelViewProj[3][2]);

        // Normalize plane equations
        for (int i = 0; i < 6; ++i) {
            float len = glm::length(glm::vec3(planes[i]));
            planes[i] /= len;
        }

        glm::vec3 cameraPos = cameraNode->getPosition();
        glm::vec3 modelSpaceCameraPos = glm::vec3(glm::inverse(modelMatrix) * glm::vec4(cameraPos, 1.0f));

        // Manage frustum freeze state for visualization
        static bool wasFrozen = false;
        if (freezeCulling) {
            if (!wasFrozen) {
                for (int i = 0; i < 6; ++i) {
                    frozenFrustumPlanes[i] = planes[i];
                }
                frozenCameraPos = modelSpaceCameraPos;
                wasFrozen = true;
            }
            for (int i = 0; i < 6; ++i) {
                planes[i] = frozenFrustumPlanes[i];
            }
            modelSpaceCameraPos = frozenCameraPos;
        } else {
            wasFrozen = false;
        }

        // 2a. Reset draw count atomic buffer
        vkCmdFillBuffer(cb, mesh.drawCountBuffer[currentFrame], 0, sizeof(uint32_t), 0);

        // Barrier: Wait for dynamic fill buffer to complete before compute shader writes
        VkBufferMemoryBarrier2 fillBarrier{};
        fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        fillBarrier.buffer = mesh.drawCountBuffer[currentFrame];
        fillBarrier.offset = 0;
        fillBarrier.size = sizeof(uint32_t);

        VkDependencyInfo fillDependency{};
        fillDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        fillDependency.bufferMemoryBarrierCount = 1;
        fillDependency.pBufferMemoryBarriers = &fillBarrier;

        vkCmdPipelineBarrier2(cb, &fillDependency);

        // 2b. Dispatch compute shader
        cullPipeline->bind(cb);

        CullPushConstants cullPcs{};
        for (int i = 0; i < 6; ++i) {
            cullPcs.frustumPlanes[i] = planes[i];
        }
        cullPcs.cameraPos = modelSpaceCameraPos;
        cullPcs.maxDrawCount = mesh.clusterCount;

        vkCmdBindDescriptorSets(
            cb,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            cullPipeline->getPipelineLayout(),
            0,
            1,
            &mesh.computeDescriptorSets[currentFrame],
            0,
            nullptr
        );

        cullPipeline->pushConstants(cb, cullPcs);

        uint32_t groupCount = (mesh.clusterCount + 63) / 64;
        vkCmdDispatch(cb, groupCount, 1, 1);

        // 2c. Synchronization Barrier: Wait for compute writes to finish before graphics draws
        VkBufferMemoryBarrier2 syncBarriers[2]{};
        
        syncBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        syncBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        syncBarriers[0].buffer = mesh.culledIndirectBuffer[currentFrame];
        syncBarriers[0].offset = 0;
        syncBarriers[0].size = sizeof(VkDrawIndexedIndirectCommand) * mesh.clusterCount;

        syncBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        syncBarriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        syncBarriers[1].buffer = mesh.drawCountBuffer[currentFrame];
        syncBarriers[1].offset = 0;
        syncBarriers[1].size = sizeof(uint32_t);

        VkDependencyInfo drawDependency{};
        drawDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        drawDependency.bufferMemoryBarrierCount = 2;
        drawDependency.pBufferMemoryBarriers = syncBarriers;

        vkCmdPipelineBarrier2(cb, &drawDependency);
    }

    // 3. Begin dynamic rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = context->getSwapChainImageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} }; // Clear: Black

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = context->getDepthImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 }; // Clear to 1.0 (far plane)

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = context->getSwapChainExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cb, &renderingInfo);

    // 4. Bind the Graphics Pipeline
    pipeline->bind(cb);

    // 5. Set Dynamic Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(context->getSwapChainExtent().width);
    viewport.height = static_cast<float>(context->getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context->getSwapChainExtent();
    vkCmdSetScissor(cb, 0, 1, &scissor);

    // 6. Push Combined MVP Matrix
    glm::mat4 mvp = viewProj * modelMatrix;
    pipeline->pushConstants(cb, mvp);

    // 7. Bind Vertex and Index Buffers
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cb, 0, 1, &activeVertexBuffer, offsets);
    vkCmdBindIndexBuffer(cb, activeIndexBuffer, 0, VK_INDEX_TYPE_UINT16); // Reconstructed indices are local uint16_t

    // 8. Draw the Mesh via Multi-Draw Indirect Count
    if (activeAsset) {
        vkCmdDrawIndexedIndirectCount(
            cb,
            activeAsset->gpuMesh.culledIndirectBuffer[currentFrame],
            0,
            activeAsset->gpuMesh.drawCountBuffer[currentFrame],
            0,
            activeAsset->gpuMesh.clusterCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }

    // 9. End Dynamic Rendering
    vkCmdEndRendering(cb);

    // 8. Transition swapchain image layout from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    VkImageMemoryBarrier2 presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    presentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    presentBarrier.dstAccessMask = 0;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.image = context->getSwapChainImages()[imageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo presentDependencyInfo{};
    presentDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDependencyInfo.imageMemoryBarrierCount = 1;
    presentDependencyInfo.pImageMemoryBarriers = &presentBarrier;

    vkCmdPipelineBarrier2(cb, &presentDependencyInfo);

    if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

void Engine::drawFrame() {
    if (framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    }

    VkDevice device = context->getDevice();
    VkQueue graphicsQueue = context->getGraphicsQueue();
    VkQueue presentQueue = context->getPresentQueue();

    // 1. Wait for frame's fence
    VkFence currentFence = context->getCurrentInFlightFence();
    vkWaitForFences(device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    // 2. Acquire next swapchain image
    uint32_t imageIndex;
    VkResult acquireResult;
    while (true) {
        acquireResult = vkAcquireNextImageKHR(
            device, 
            context->getSwapChain(), 
            UINT64_MAX, 
            context->getCurrentImageAvailableSemaphore(), 
            VK_NULL_HANDLE, 
            &imageIndex
        );

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            continue;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }
        break;
    }

    // Only reset fence if we are proceeding with submission
    vkResetFences(device, 1, &currentFence);

    // 3. Reset command buffer
    VkCommandBuffer cb = context->getCurrentCommandBuffer();
    vkResetCommandBuffer(cb, 0);

    // 4. Record commands
    recordCommandBuffer(cb, imageIndex);

    // 5. Submit command buffer to queue
    VkCommandBufferSubmitInfo cbSubmitInfo{};
    cbSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSubmitInfo.commandBuffer = cb;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = context->getCurrentImageAvailableSemaphore();
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = context->getRenderFinishedSemaphore(imageIndex);
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cbSubmitInfo;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo, currentFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    // 6. Present image
    VkSwapchainKHR swapChain = context->getSwapChain();
    VkSemaphore renderFinishedSem = context->getRenderFinishedSemaphore(imageIndex);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSem;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 7. Advance frame index
    context->advanceFrame();
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
}

void Engine::handleWindowRefresh() {
    // Calculate delta time
    float currentFrameTime = static_cast<float>(glfwGetTime());
    float deltaTime = currentFrameTime - lastFrameTime;
    lastFrameTime = currentFrameTime;

    // Update Camera and Scene Graph
    cameraNode->update(deltaTime, window);
    rootNode->update(deltaTime);
    rootNode->updateWorldMatrix(glm::mat4(1.0f));

    drawFrame();
}

void Engine::uploadMesh(const MeshNode& mesh, GPUMesh& gpuMesh) {
    VulkanContext& ctx = *context;
    
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    
    if (vertices.empty() || indices.empty()) {
        return;
    }
    
    size_t max_vertices = 64;
    size_t max_triangles = 126;
    float cone_weight = 0.0f;
    
    size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
    std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
    std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);
    
    size_t meshlet_count = meshopt_buildMeshlets(
        meshlets.data(),
        meshlet_vertices.data(),
        meshlet_triangles.data(),
        indices.data(),
        indices.size(),
        &vertices[0].pos.x,
        vertices.size(),
        sizeof(MeshVertex),
        max_vertices,
        max_triangles,
        cone_weight
    );
    
    meshlets.resize(meshlet_count);
    
    std::cout << "  [Meshlets Info] Model: '" << mesh.getModelPath() << "'" << std::endl;
    std::cout << "    Triangles: " << (indices.size() / 3) << " -> Meshlets: " << meshlet_count << std::endl;
    
    size_t total_local_vertices = 0;
    size_t total_local_triangles = 0;
    for (size_t i = 0; i < meshlet_count; ++i) {
        total_local_vertices += meshlets[i].vertex_count;
        total_local_triangles += meshlets[i].triangle_count;
    }
    std::cout << "    Avg Vertices/Meshlet: " << (static_cast<double>(total_local_vertices) / meshlet_count) 
              << ", Avg Triangles/Meshlet: " << (static_cast<double>(total_local_triangles) / meshlet_count) << std::endl;

    // Flatten the meshlets into CPU arrays for standard rendering
    std::vector<MeshVertex> flatVertices;
    std::vector<uint16_t> flatIndices; // Local uint16_t indices
    std::vector<VkDrawIndexedIndirectCommand> indirectCommands;
    
    flatVertices.reserve(meshlet_count * max_vertices);
    flatIndices.reserve(meshlet_count * max_triangles * 3);
    indirectCommands.reserve(meshlet_count);
    
    for (size_t i = 0; i < meshlet_count; ++i) {
        const auto& meshlet = meshlets[i];
        
        uint32_t baseVertexOffset = static_cast<uint32_t>(flatVertices.size());
        uint32_t baseIndexOffset = static_cast<uint32_t>(flatIndices.size());
        
        // Copy meshlet vertices
        for (uint32_t v = 0; v < meshlet.vertex_count; ++v) {
            uint32_t origVertexIdx = meshlet_vertices[meshlet.vertex_offset + v];
            MeshVertex vertex = vertices[origVertexIdx];
            flatVertices.push_back(vertex);
        }
        
        // Copy meshlet indices (local uint16_t!)
        for (uint32_t t = 0; t < meshlet.triangle_count; ++t) {
            uint16_t localIdx0 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 0];
            uint16_t localIdx1 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 1];
            uint16_t localIdx2 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 2];
            
            flatIndices.push_back(localIdx0);
            flatIndices.push_back(localIdx1);
            flatIndices.push_back(localIdx2);
        }
        
        // Construct indirect draw command
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = meshlet.triangle_count * 3;
        cmd.instanceCount = 1;
        cmd.firstIndex = baseIndexOffset;
        cmd.vertexOffset = static_cast<int32_t>(baseVertexOffset);
        cmd.firstInstance = static_cast<uint32_t>(i);
        
        indirectCommands.push_back(cmd);
    }
    
    // 1. Vertex Buffer
    VkDeviceSize vertexBufferSize = sizeof(MeshVertex) * flatVertices.size();
    
    VkBuffer stagingVertexBuffer;
    VmaAllocation stagingVertexAllocation;
    ctx.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingVertexBuffer,
        stagingVertexAllocation
    );
    
    void* vertexData;
    vmaMapMemory(ctx.getAllocator(), stagingVertexAllocation, &vertexData);
    memcpy(vertexData, flatVertices.data(), vertexBufferSize);
    vmaUnmapMemory(ctx.getAllocator(), stagingVertexAllocation);
    
    ctx.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        gpuMesh.vertexBuffer,
        gpuMesh.vertexAllocation
    );
    
    ctx.copyBuffer(stagingVertexBuffer, gpuMesh.vertexBuffer, vertexBufferSize);
    vmaDestroyBuffer(ctx.getAllocator(), stagingVertexBuffer, stagingVertexAllocation);
    
    // 2. Index Buffer
    VkDeviceSize indexBufferSize = sizeof(uint16_t) * flatIndices.size();
    
    VkBuffer stagingIndexBuffer;
    VmaAllocation stagingIndexAllocation;
    ctx.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingIndexBuffer,
        stagingIndexAllocation
    );
    
    void* indexData;
    vmaMapMemory(ctx.getAllocator(), stagingIndexAllocation, &indexData);
    memcpy(indexData, flatIndices.data(), indexBufferSize);
    vmaUnmapMemory(ctx.getAllocator(), stagingIndexAllocation);
    
    ctx.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        gpuMesh.indexBuffer,
        gpuMesh.indexAllocation
    );
    
    ctx.copyBuffer(stagingIndexBuffer, gpuMesh.indexBuffer, indexBufferSize);
    vmaDestroyBuffer(ctx.getAllocator(), stagingIndexBuffer, stagingIndexAllocation);
    
    // 3. Indirect Buffer
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndexedIndirectCommand) * indirectCommands.size();
    gpuMesh.clusterCount = static_cast<uint32_t>(indirectCommands.size());
    
    VkBuffer stagingIndirectBuffer;
    VmaAllocation stagingIndirectAllocation;
    ctx.createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingIndirectBuffer,
        stagingIndirectAllocation
    );
    
    void* indirectData;
    vmaMapMemory(ctx.getAllocator(), stagingIndirectAllocation, &indirectData);
    memcpy(indirectData, indirectCommands.data(), indirectBufferSize);
    vmaUnmapMemory(ctx.getAllocator(), stagingIndirectAllocation);
    
    ctx.createBuffer(
        indirectBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        gpuMesh.indirectBuffer,
        gpuMesh.indirectAllocation
    );
    
    ctx.copyBuffer(stagingIndirectBuffer, gpuMesh.indirectBuffer, indirectBufferSize);
    vmaDestroyBuffer(ctx.getAllocator(), stagingIndirectBuffer, stagingIndirectAllocation);

    // 4. Compute Meshlet Bounds (Spheres & Cones) using meshoptimizer
    std::vector<MeshletBounds> boundsList;
    boundsList.reserve(meshlet_count);

    for (size_t i = 0; i < meshlet_count; ++i) {
        const auto& meshlet = meshlets[i];

        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshlet_vertices[meshlet.vertex_offset],
            &meshlet_triangles[meshlet.triangle_offset],
            meshlet.triangle_count,
            &vertices[0].pos.x,
            vertices.size(),
            sizeof(MeshVertex)
        );

        MeshletBounds b{};
        b.sphereCenterRadius = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
        b.coneAxisCutoff = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
        boundsList.push_back(b);
    }

    VkDeviceSize boundsBufferSize = sizeof(MeshletBounds) * boundsList.size();

    VkBuffer stagingBoundsBuffer;
    VmaAllocation stagingBoundsAllocation;
    ctx.createBuffer(
        boundsBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        stagingBoundsBuffer,
        stagingBoundsAllocation
    );

    void* boundsData;
    vmaMapMemory(ctx.getAllocator(), stagingBoundsAllocation, &boundsData);
    memcpy(boundsData, boundsList.data(), boundsBufferSize);
    vmaUnmapMemory(ctx.getAllocator(), stagingBoundsAllocation);

    ctx.createBuffer(
        boundsBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        gpuMesh.boundsBuffer,
        gpuMesh.boundsAllocation
    );

    ctx.copyBuffer(stagingBoundsBuffer, gpuMesh.boundsBuffer, boundsBufferSize);
    vmaDestroyBuffer(ctx.getAllocator(), stagingBoundsBuffer, stagingBoundsAllocation);

    // 5. Allocate Culled Indirect Buffers and Draw Count Buffers (Double-Buffered)
    for (int i = 0; i < 2; ++i) {
        // Culled Indirect Buffer: compute output, MDI drawing input
        ctx.createBuffer(
            indirectBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            gpuMesh.culledIndirectBuffer[i],
            gpuMesh.culledIndirectAllocation[i]
        );

        // Draw Count Buffer: compute atomic output, count draw command input
        ctx.createBuffer(
            sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            0,
            gpuMesh.drawCountBuffer[i],
            gpuMesh.drawCountAllocation[i]
        );
    }
}

void Engine::createComputeDescriptorSets(GPUMesh& gpuMesh) {
    VkDevice device = context->getDevice();
    VkDescriptorSetLayout layout = cullPipeline->getDescriptorSetLayout();

    // Allocate 2 descriptor sets (one for each frame in flight)
    std::array<VkDescriptorSetLayout, 2> layouts = { layout, layout };
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = computeDescriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, gpuMesh.computeDescriptorSets) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate compute descriptor sets!");
    }

    // Write descriptor sets
    for (int i = 0; i < 2; ++i) {
        VkDescriptorBufferInfo inputInfo{};
        inputInfo.buffer = gpuMesh.indirectBuffer;
        inputInfo.offset = 0;
        inputInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo outputInfo{};
        outputInfo.buffer = gpuMesh.culledIndirectBuffer[i];
        outputInfo.offset = 0;
        outputInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo countInfo{};
        countInfo.buffer = gpuMesh.drawCountBuffer[i];
        countInfo.offset = 0;
        countInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo boundsInfo{};
        boundsInfo.buffer = gpuMesh.boundsBuffer;
        boundsInfo.offset = 0;
        boundsInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};
        
        // Binding 0: Input Commands
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = gpuMesh.computeDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &inputInfo;

        // Binding 1: Output Commands
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = gpuMesh.computeDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &outputInfo;

        // Binding 2: Draw Count
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = gpuMesh.computeDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &countInfo;

        // Binding 3: Meshlet Bounds
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = gpuMesh.computeDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &boundsInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
