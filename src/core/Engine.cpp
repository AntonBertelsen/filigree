#include "Engine.hpp"

#include <iostream>
#include <stdexcept>
#include <chrono>

Engine::Engine() {
    initWindow();
    context = std::make_unique<VulkanContext>(window);
    pipeline = std::make_unique<StandardPipeline>(*context);
    
    rootNode = std::make_unique<Node>();
    
    auto camera = std::make_unique<CameraNode>();
    cameraNode = camera.get();
    rootNode->addChild(std::move(camera));
    
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
        }
    }
    models.clear();

    // Release pipeline and context
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

    // 2. Begin dynamic rendering
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

    // 3. Bind the Graphics Pipeline
    pipeline->bind(cb);

    // 4. Set Dynamic Viewport & Scissor
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

    // 5. Push Combined MVP Matrix
    float aspect = static_cast<float>(context->getSwapChainExtent().width) / static_cast<float>(context->getSwapChainExtent().height);
    glm::mat4 viewProj = cameraNode->getProjectionMatrix(aspect) * cameraNode->getViewMatrix();
    
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    VkBuffer activeVertexBuffer = VK_NULL_HANDLE;
    VkBuffer activeIndexBuffer = VK_NULL_HANDLE;
    uint32_t activeIndexCount = 0;

    if (activeModelIndex < models.size()) {
        const auto& activeAsset = models[activeModelIndex];
        modelMatrix = activeAsset.sceneNode->getWorldMatrix();
        activeVertexBuffer = activeAsset.gpuMesh.vertexBuffer;
        activeIndexBuffer = activeAsset.gpuMesh.indexBuffer;
        activeIndexCount = activeAsset.gpuMesh.indexCount;
    }

    glm::mat4 mvp = viewProj * modelMatrix;
    pipeline->pushConstants(cb, mvp);

    // 6. Bind Vertex and Index Buffers
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cb, 0, 1, &activeVertexBuffer, offsets);
    vkCmdBindIndexBuffer(cb, activeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // 7. Draw the Mesh!
    vkCmdDrawIndexed(cb, activeIndexCount, 1, 0, 0, 0);

    // 7. End Dynamic Rendering
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
    
    // 1. Vertex Buffer
    VkDeviceSize vertexBufferSize = sizeof(MeshVertex) * mesh.getVertices().size();
    
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
    memcpy(vertexData, mesh.getVertices().data(), vertexBufferSize);
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
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * mesh.getIndices().size();
    gpuMesh.indexCount = static_cast<uint32_t>(mesh.getIndices().size());
    
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
    memcpy(indexData, mesh.getIndices().data(), indexBufferSize);
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
}
