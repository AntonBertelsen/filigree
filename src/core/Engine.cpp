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
    
    // Load Stanford Bunny OBJ Mesh Model
    std::cout << "Loading Bunny model..." << std::endl;
    auto bunny = std::make_unique<MeshNode>("assets/models/bunny.obj");
    bunnyNode = bunny.get();
    bunnyNode->setPosition(glm::vec3(0.0f, -0.7f, 0.0f));
    bunnyNode->setScale(glm::vec3(1.0f));
    rootNode->addChild(std::move(bunny));
    
    // Load Stanford Lucy OBJ Mesh Model
    std::cout << "Loading Lucy model..." << std::endl;
    auto lucy = std::make_unique<MeshNode>("assets/models/lucy.obj");
    lucyNode = lucy.get();
    lucyNode->setPosition(glm::vec3(0.0f, -0.8f, 0.0f));
    lucyNode->setScale(glm::vec3(0.002f));
    rootNode->addChild(std::move(lucy));
    
    // Upload meshes to GPU using VMA
    std::cout << "Uploading Bunny mesh to GPU..." << std::endl;
    uploadMesh(*bunnyNode, bunnyMesh);
    std::cout << "Uploading Lucy mesh to GPU..." << std::endl;
    uploadMesh(*lucyNode, lucyMesh);
    
    lastFrameTime = static_cast<float>(glfwGetTime());
}

Engine::~Engine() {
    cleanup();
}

void Engine::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Filigree Engine", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
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

        // Poll Tab key to toggle between Bunny and Lucy
        bool currentTabState = (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS);
        if (currentTabState && !tabWasPressed) {
            showLucy = !showLucy;
            std::cout << "Active model switched to: " << (showLucy ? "Lucy" : "Bunny") << std::endl;
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
    bunnyNode = nullptr;
    lucyNode = nullptr;

    // Release GPU Mesh buffers
    if (context) {
        VmaAllocator allocator = context->getAllocator();
        if (bunnyMesh.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, bunnyMesh.vertexBuffer, bunnyMesh.vertexAllocation);
            bunnyMesh.vertexBuffer = VK_NULL_HANDLE;
        }
        if (bunnyMesh.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, bunnyMesh.indexBuffer, bunnyMesh.indexAllocation);
            bunnyMesh.indexBuffer = VK_NULL_HANDLE;
        }
        if (lucyMesh.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, lucyMesh.vertexBuffer, lucyMesh.vertexAllocation);
            lucyMesh.vertexBuffer = VK_NULL_HANDLE;
        }
        if (lucyMesh.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, lucyMesh.indexBuffer, lucyMesh.indexAllocation);
            lucyMesh.indexBuffer = VK_NULL_HANDLE;
        }
    }

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

    // 1. Transition swapchain image layout from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.image = context->getSwapChainImages()[imageIndex];
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cb, &dependencyInfo);

    // 2. Begin dynamic rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = context->getSwapChainImageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} }; // Clear: Black

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = context->getSwapChainExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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
    glm::mat4 modelMatrix = showLucy ? lucyNode->getWorldMatrix() : bunnyNode->getWorldMatrix();
    glm::mat4 mvp = viewProj * modelMatrix;
    pipeline->pushConstants(cb, mvp);

    // 6. Bind Vertex and Index Buffers
    VkDeviceSize offsets[] = {0};
    VkBuffer activeVertexBuffer = showLucy ? lucyMesh.vertexBuffer : bunnyMesh.vertexBuffer;
    VkBuffer activeIndexBuffer = showLucy ? lucyMesh.indexBuffer : bunnyMesh.indexBuffer;
    uint32_t activeIndexCount = showLucy ? lucyMesh.indexCount : bunnyMesh.indexCount;

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
    presentBarrier.subresourceRange = imageBarrier.subresourceRange;

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
    VkDevice device = context->getDevice();
    VkQueue graphicsQueue = context->getGraphicsQueue();
    VkQueue presentQueue = context->getPresentQueue();

    // 1. Wait for frame's fence
    VkFence currentFence = context->getCurrentInFlightFence();
    vkWaitForFences(device, 1, &currentFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &currentFence);

    // 2. Acquire next swapchain image
    uint32_t imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        device, 
        context->getSwapChain(), 
        UINT64_MAX, 
        context->getCurrentImageAvailableSemaphore(), 
        VK_NULL_HANDLE, 
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return; // handle swapchain recreation if resize was enabled
    } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

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
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // handle swapchain recreation if resize was enabled
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 7. Advance frame index
    context->advanceFrame();
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
