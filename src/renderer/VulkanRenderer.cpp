#include "VulkanRenderer.hpp"
#include "core/Engine.hpp"
#include "renderer/pipelines/HzbPipeline.hpp"
#include "renderer/pipelines/DebugPipeline.hpp"
#include <stdexcept>
#include <array>

VulkanRenderer::VulkanRenderer(VulkanContext& context, StandardPipeline& pipeline, CullPipeline& cullPipeline)
    : context(context), pipeline(pipeline), cullPipeline(cullPipeline) {
    boundsPipeline = std::make_unique<BoundsPipeline>(context, cullPipeline.getDescriptorSetLayout());
    visBufferPipeline = std::make_unique<VisBufferPipeline>(context);
    resolvePipeline = std::make_unique<ResolvePipeline>(context);
    createVisBufferResources();
}

VulkanRenderer::~VulkanRenderer() {
    destroyVisBufferResources();
}

void VulkanRenderer::recreateVisBuffer() {
    destroyVisBufferResources();
    createVisBufferResources();
}

void VulkanRenderer::createVisBufferResources() {
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();
    VkExtent2D extent = context.getSwapChainExtent();

    // 1. Create Image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32_UINT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocInfo.priority = 1.0f;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &visBufferImage, &visBufferAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer image!");
    }

    // 2. Create ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = visBufferImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &visBufferImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer imageView!");
    }

    // 3. Create Sampler (Nearest filtering, clamp to edge)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &visBufferSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VisBuffer sampler!");
    }
}

void VulkanRenderer::destroyVisBufferResources() {
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    if (visBufferSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, visBufferSampler, nullptr);
        visBufferSampler = VK_NULL_HANDLE;
    }
    if (visBufferImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, visBufferImageView, nullptr);
        visBufferImageView = VK_NULL_HANDLE;
    }
    if (visBufferImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, visBufferImage, visBufferAllocation);
        visBufferImage = VK_NULL_HANDLE;
        visBufferAllocation = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::drawFrame(Engine& engine) {
    VkDevice device = context.getDevice();
    VkQueue graphicsQueue = context.getGraphicsQueue();
    VkQueue presentQueue = context.getPresentQueue();

    VkFence currentFence = context.getCurrentInFlightFence();

    // 1. Wait for previous frame's GPU fence
    vkWaitForFences(device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    // 2. Recreate Swapchain if required (e.g. dynamic resize detected on present)
    uint32_t imageIndex;
    while (true) {
        if (engine.framebufferResized) {
            engine.framebufferResized = false;
            engine.recreateSwapChain();
            currentFence = context.getCurrentInFlightFence();
        }

        VkResult acquireResult = vkAcquireNextImageKHR(
            device, 
            context.getSwapChain(), 
            UINT64_MAX, 
            context.getCurrentImageAvailableSemaphore(), 
            VK_NULL_HANDLE, 
            &imageIndex
        );

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            engine.recreateSwapChain();
            continue;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }
        break;
    }

    // Only reset fence if we are proceeding with submission
    vkResetFences(device, 1, &currentFence);

    // 3. Reset command buffer
    VkCommandBuffer cb = context.getCurrentCommandBuffer();
    vkResetCommandBuffer(cb, 0);

    // 4. Record commands
    recordCommandBuffer(cb, imageIndex, engine);

    // 5. Submit command buffer to queue
    VkCommandBufferSubmitInfo cbSubmitInfo{};
    cbSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSubmitInfo.commandBuffer = cb;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = context.getCurrentImageAvailableSemaphore();
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = context.getRenderFinishedSemaphore(imageIndex);
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
    VkSwapchainKHR swapChain = context.getSwapChain();
    VkSemaphore renderFinishedSem = context.getRenderFinishedSemaphore(imageIndex);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSem;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || engine.framebufferResized) {
        engine.framebufferResized = false;
        engine.recreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 7. Advance frame index
    context.advanceFrame();
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, Engine& engine) {
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
    barriers[0].image = context.getSwapChainImages()[imageIndex];
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
    barriers[1].image = context.getDepthImage();
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

    // 2. GPU Compute Pass for Culling (executed outside of rendering pass if Nanite rendering is enabled)
    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();
    
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    VkBuffer activeVertexBuffer = VK_NULL_HANDLE;
    VkBuffer activeIndexBuffer = VK_NULL_HANDLE;
    
    uint32_t currentFrame = context.getCurrentFrameIndex();
    const Engine::ModelAsset* activeAsset = nullptr;

    if (engine.activeModelIndex < engine.models.size()) {
        activeAsset = &engine.models[engine.activeModelIndex];
        modelMatrix = activeAsset->sceneNode->getWorldMatrix();
        activeVertexBuffer = activeAsset->gpuMesh.vertexBuffer;
        activeIndexBuffer = activeAsset->gpuMesh.indexBuffer;
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

        glm::vec3 cameraPos = engine.cameraNode->getPosition();
        glm::vec3 modelSpaceCameraPos = glm::vec3(glm::inverse(modelMatrix) * glm::vec4(cameraPos, 1.0f));

        // Manage frustum freeze state for visualization
        static bool wasFrozen = false;
        static CullPushConstants frozenCullPcs{};

        CullPushConstants cullPcs{};

        if (engine.freezeCulling) {
            if (!wasFrozen) {
                frozenCullPcs.modelViewProj = viewProj * modelMatrix;
                for (int i = 0; i < 6; ++i) {
                    frozenCullPcs.frustumPlanes[i] = planes[i];
                }
                frozenCullPcs.cameraPos = modelSpaceCameraPos;
                
                // Calculate model scale
                float scaleX = glm::length(glm::vec3(modelMatrix[0]));
                float scaleY = glm::length(glm::vec3(modelMatrix[1]));
                float scaleZ = glm::length(glm::vec3(modelMatrix[2]));
                frozenCullPcs.modelScale = std::max({scaleX, scaleY, scaleZ});
                
                // Calculate hzbParams (proj[0][0], proj[1][1], proj[2][2], proj[3][2])
                frozenCullPcs.hzbParams = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
                
                wasFrozen = true;
            }
            cullPcs = frozenCullPcs;
        } else {
            wasFrozen = false;
            
            cullPcs.modelViewProj = viewProj * modelMatrix;
            for (int i = 0; i < 6; ++i) {
                cullPcs.frustumPlanes[i] = planes[i];
            }
            cullPcs.cameraPos = modelSpaceCameraPos;
            
            float scaleX = glm::length(glm::vec3(modelMatrix[0]));
            float scaleY = glm::length(glm::vec3(modelMatrix[1]));
            float scaleZ = glm::length(glm::vec3(modelMatrix[2]));
            cullPcs.modelScale = std::max({scaleX, scaleY, scaleZ});
            
            cullPcs.hzbParams = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
        }

        cullPcs.maxDrawCount = mesh.clusterCount;
        cullPcs.hzbWidth = VulkanContext::HZB_WIDTH;
        cullPcs.hzbHeight = VulkanContext::HZB_HEIGHT;
        cullPcs.maxMipLevel = VulkanContext::HZB_MIP_LEVELS - 1;
        cullPcs.hzbCullingEnabled = engine.hzbCullingEnabled ? 1 : 0;
        cullPcs.lodThreshold = engine.lodThreshold;
        cullPcs.viewportHeight = static_cast<float>(context.getSwapChainExtent().height);
        cullPcs.lodEnabled = engine.lodEnabled ? 1 : 0;
        cullPcs.padding2 = 0;

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
        cullPipeline.bind(cb);

        vkCmdBindDescriptorSets(
            cb,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            cullPipeline.getPipelineLayout(),
            0,
            1,
            &mesh.computeDescriptorSets[currentFrame],
            0,
            nullptr
        );

        cullPipeline.pushConstants(cb, cullPcs);

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
    if (activeAsset && engine.renderModeNanite) {
        // --- Pass 1: Render Nanite Meshlets to VisBuffer ---

        // Image Barrier: Transition VisBuffer to COLOR_ATTACHMENT_OPTIMAL
        VkImageMemoryBarrier2 visBarrier{};
        visBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        visBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        visBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        visBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        visBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        visBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Discard old contents
        visBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        visBarrier.image = visBufferImage;
        visBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        visBarrier.subresourceRange.baseMipLevel = 0;
        visBarrier.subresourceRange.levelCount = 1;
        visBarrier.subresourceRange.baseArrayLayer = 0;
        visBarrier.subresourceRange.layerCount = 1;

        VkDependencyInfo visDependency{};
        visDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        visDependency.imageMemoryBarrierCount = 1;
        visDependency.pImageMemoryBarriers = &visBarrier;
        vkCmdPipelineBarrier2(cb, &visDependency);

        VkRenderingAttachmentInfo visColorAttachment{};
        visColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        visColorAttachment.imageView = visBufferImageView;
        visColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        visColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        visColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        visColorAttachment.clearValue.color.uint32[0] = 0xFFFFFFFFu;
        visColorAttachment.clearValue.color.uint32[1] = 0xFFFFFFFFu;
        visColorAttachment.clearValue.color.uint32[2] = 0xFFFFFFFFu;
        visColorAttachment.clearValue.color.uint32[3] = 0xFFFFFFFFu;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = context.getDepthImageView();
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = context.getSwapChainExtent();
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &visColorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cb, &renderingInfo);

        // Bind VisBuffer Pipeline
        visBufferPipeline->bind(cb);

        // Set Viewport & Scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context.getSwapChainExtent().width);
        viewport.height = static_cast<float>(context.getSwapChainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = context.getSwapChainExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        // Push Constants
        VisBufferPipeline::VisBufferPushConstants visPcs{};
        visPcs.viewProj = viewProj * modelMatrix;
        visBufferPipeline->pushConstants(cb, visPcs);

        // Bind Meshlet buffers
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, &activeVertexBuffer, offsets);
        vkCmdBindIndexBuffer(cb, activeIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        // Draw via Multi-Draw Indirect Count
        vkCmdDrawIndexedIndirectCount(
            cb,
            activeAsset->gpuMesh.culledIndirectBuffer[currentFrame],
            0,
            activeAsset->gpuMesh.drawCountBuffer[currentFrame],
            0,
            activeAsset->gpuMesh.clusterCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );

        vkCmdEndRendering(cb);

        // Image Barrier: Transition VisBuffer to SHADER_READ_ONLY_OPTIMAL for resolve pass
        VkImageMemoryBarrier2 readBarrier{};
        readBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        readBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        readBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        readBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readBarrier.image = visBufferImage;
        readBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        readBarrier.subresourceRange.baseMipLevel = 0;
        readBarrier.subresourceRange.levelCount = 1;
        readBarrier.subresourceRange.baseArrayLayer = 0;
        readBarrier.subresourceRange.layerCount = 1;

        VkDependencyInfo readDependency{};
        readDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        readDependency.imageMemoryBarrierCount = 1;
        readDependency.pImageMemoryBarriers = &readBarrier;
        vkCmdPipelineBarrier2(cb, &readDependency);
    }

    // --- Pass 2: Main Swapchain Pass (Resolve VisBuffer & Shading) ---
    VkRenderingAttachmentInfo swapColorAttachment{};
    swapColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
    swapColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    swapColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapColorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} }; // Clear swapchain to black

    VkRenderingAttachmentInfo swapDepthAttachment{};
    swapDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapDepthAttachment.imageView = context.getDepthImageView();
    swapDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    // In Nanite mode, we load the depth built in Pass 1 to allow debug bounding spheres to depth test correctly.
    // In traditional mode, we clear depth since we will rasterize directly to the swapchain.
    swapDepthAttachment.loadOp = (activeAsset && engine.renderModeNanite) ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    swapDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo swapRenderingInfo{};
    swapRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    swapRenderingInfo.renderArea.offset = {0, 0};
    swapRenderingInfo.renderArea.extent = context.getSwapChainExtent();
    swapRenderingInfo.layerCount = 1;
    swapRenderingInfo.colorAttachmentCount = 1;
    swapRenderingInfo.pColorAttachments = &swapColorAttachment;
    swapRenderingInfo.pDepthAttachment = &swapDepthAttachment;

    vkCmdBeginRendering(cb, &swapRenderingInfo);

    if (activeAsset) {
        // Set Viewport & Scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context.getSwapChainExtent().width);
        viewport.height = static_cast<float>(context.getSwapChainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = context.getSwapChainExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        if (engine.renderModeNanite) {
            // Draw Fullscreen Resolve Quad
            resolvePipeline->updateDescriptorSets(
                currentFrame,
                visBufferImageView,
                visBufferSampler,
                activeAsset->gpuMesh.vertexBuffer,
                activeAsset->gpuMesh.indexBuffer,
                activeAsset->gpuMesh.indirectBuffer
            );

            resolvePipeline->bind(cb);

            VkDescriptorSet resolveSets[] = { resolvePipeline->getDescriptorSet(currentFrame) };
            vkCmdBindDescriptorSets(
                cb,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                resolvePipeline->getPipelineLayout(),
                0,
                1,
                resolveSets,
                0,
                nullptr
            );

            ResolvePipeline::ResolvePushConstants resolvePcs{};
            resolvePcs.viewProj = viewProj * modelMatrix;
            resolvePcs.viewportSize = glm::vec2(viewport.width, viewport.height);
            resolvePcs.isNaniteMode = 1;
            resolvePcs.debugMode = engine.visBufferDebugMode;
            resolvePipeline->pushConstants(cb, resolvePcs);

            // Draw fullscreen quad (3 vertices, 1 instance)
            vkCmdDraw(cb, 3, 1, 0, 0);
        } else {
            // --- Fallback: Render Traditional Geometry Directly to Swapchain ---
            pipeline.bind(cb);

            StandardPipeline::StandardPushConstants standardPcs{};
            standardPcs.viewProj = viewProj * modelMatrix;
            standardPcs.isNaniteMode = 0;
            pipeline.pushConstants(cb, standardPcs);

            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cb, 0, 1, &activeAsset->gpuMesh.traditionalVertexBuffer, offsets);
            vkCmdBindIndexBuffer(cb, activeAsset->gpuMesh.traditionalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cb, activeAsset->gpuMesh.traditionalIndexCount, 1, 0, 0, 0);
        }

        // 4c. Bounding spheres debug rendering if enabled
        if (engine.drawBoundingSpheres) {
            boundsPipeline->bind(cb);

            boundsPipeline->pushConstants(cb, viewProj * modelMatrix);

            vkCmdBindDescriptorSets(
                cb,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                boundsPipeline->getPipelineLayout(),
                0,
                1,
                &activeAsset->gpuMesh.computeDescriptorSets[currentFrame],
                0,
                nullptr
            );

            // Draw 3 circles * 16 segments * 2 vertices = 96 vertices
            vkCmdDraw(cb, 96, activeAsset->gpuMesh.clusterCount, 0, 0);
        }
    }

    vkCmdEndRendering(cb);

    // 6. Generate Hierarchical Z-Buffer (HZB) for the next frame's culling
    // Transition depth buffer to SHADER_READ_ONLY_OPTIMAL and current HZB to GENERAL layout
    VkImageMemoryBarrier2 depthToReadBarrier{};
    depthToReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    depthToReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthToReadBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthToReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    depthToReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    depthToReadBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthToReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthToReadBarrier.image = context.getDepthImage();
    depthToReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthToReadBarrier.subresourceRange.baseMipLevel = 0;
    depthToReadBarrier.subresourceRange.levelCount = 1;
    depthToReadBarrier.subresourceRange.baseArrayLayer = 0;
    depthToReadBarrier.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier2 hzbToGeneralBarrier{};
    hzbToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    hzbToGeneralBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    hzbToGeneralBarrier.srcAccessMask = 0;
    hzbToGeneralBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    hzbToGeneralBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    hzbToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hzbToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    hzbToGeneralBarrier.image = context.getHzbImage(currentFrame);
    hzbToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    hzbToGeneralBarrier.subresourceRange.baseMipLevel = 0;
    hzbToGeneralBarrier.subresourceRange.levelCount = VulkanContext::HZB_MIP_LEVELS;
    hzbToGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    hzbToGeneralBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo initDep{};
    initDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    VkImageMemoryBarrier2 initBarriers[2] = { depthToReadBarrier, hzbToGeneralBarrier };
    initDep.imageMemoryBarrierCount = 2;
    initDep.pImageMemoryBarriers = initBarriers;
    vkCmdPipelineBarrier2(cb, &initDep);

    // Run Level 0 and mip downsampling dispatches ONLY if culling is not frozen
    if (!engine.freezeCulling) {
        // Run Level 0 downsampling (Read from Depth image -> Write to HZB Level 0)
        VkExtent2D swapExtent = context.getSwapChainExtent();
        engine.hzbPipeline->recordDispatch(cb, currentFrame, 0, swapExtent.width, swapExtent.height, -1);

        // Run Levels 1 to 10 downsampling (Read from HZB Level L-1 -> Write to HZB Level L)
        for (uint32_t L = 1; L < VulkanContext::HZB_MIP_LEVELS; L++) {
            VkImageMemoryBarrier2 lvlBarrier{};
            lvlBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            lvlBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            lvlBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            lvlBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            lvlBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            lvlBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            lvlBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            lvlBarrier.image = context.getHzbImage(currentFrame);
            lvlBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            lvlBarrier.subresourceRange.baseMipLevel = L - 1;
            lvlBarrier.subresourceRange.levelCount = 1;
            lvlBarrier.subresourceRange.baseArrayLayer = 0;
            lvlBarrier.subresourceRange.layerCount = 1;

            VkDependencyInfo lvlDep{};
            lvlDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            lvlDep.imageMemoryBarrierCount = 1;
            lvlDep.pImageMemoryBarriers = &lvlBarrier;
            vkCmdPipelineBarrier2(cb, &lvlDep);

            uint32_t srcWidth = std::max(1u, VulkanContext::HZB_WIDTH >> (L - 1));
            uint32_t srcHeight = std::max(1u, VulkanContext::HZB_HEIGHT >> (L - 1));
            engine.hzbPipeline->recordDispatch(cb, currentFrame, L, srcWidth, srcHeight, L - 1);
        }
    }


    // Transition depth buffer back to attachment layout and HZB to shader read optimal layout
    VkImageMemoryBarrier2 depthToAttachmentBarrier{};
    depthToAttachmentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    depthToAttachmentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    depthToAttachmentBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    depthToAttachmentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthToAttachmentBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthToAttachmentBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthToAttachmentBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthToAttachmentBarrier.image = context.getDepthImage();
    depthToAttachmentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthToAttachmentBarrier.subresourceRange.baseMipLevel = 0;
    depthToAttachmentBarrier.subresourceRange.levelCount = 1;
    depthToAttachmentBarrier.subresourceRange.baseArrayLayer = 0;
    depthToAttachmentBarrier.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier2 hzbToReadOnlyBarrier{};
    hzbToReadOnlyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    hzbToReadOnlyBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    hzbToReadOnlyBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    hzbToReadOnlyBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    hzbToReadOnlyBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    hzbToReadOnlyBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    hzbToReadOnlyBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hzbToReadOnlyBarrier.image = context.getHzbImage(currentFrame);
    hzbToReadOnlyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    hzbToReadOnlyBarrier.subresourceRange.baseMipLevel = 0;
    hzbToReadOnlyBarrier.subresourceRange.levelCount = VulkanContext::HZB_MIP_LEVELS;
    hzbToReadOnlyBarrier.subresourceRange.baseArrayLayer = 0;
    hzbToReadOnlyBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo finalDep{};
    finalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    VkImageMemoryBarrier2 finalBarriers[2] = { depthToAttachmentBarrier, hzbToReadOnlyBarrier };
    finalDep.imageMemoryBarrierCount = 2;
    finalDep.pImageMemoryBarriers = finalBarriers;
    vkCmdPipelineBarrier2(cb, &finalDep);

    // 6.5. Draw HZB Debug Fullscreen Quad if debug view is enabled
    if (engine.debugVisualiseHzb) {
        VkRenderingAttachmentInfo debugColorAttachment{};
        debugColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        debugColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
        debugColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        debugColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear the mesh rendering
        debugColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        debugColorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

        VkRenderingInfo debugRenderingInfo{};
        debugRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        debugRenderingInfo.renderArea.offset = {0, 0};
        debugRenderingInfo.renderArea.extent = context.getSwapChainExtent();
        debugRenderingInfo.layerCount = 1;
        debugRenderingInfo.colorAttachmentCount = 1;
        debugRenderingInfo.pColorAttachments = &debugColorAttachment;
        debugRenderingInfo.pDepthAttachment = nullptr;

        vkCmdBeginRendering(cb, &debugRenderingInfo);

        engine.debugPipeline->bind(cb);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context.getSwapChainExtent().width);
        viewport.height = static_cast<float>(context.getSwapChainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = context.getSwapChainExtent();
        vkCmdSetScissor(cb, 0, 1, &scissor);

        VkDescriptorSet ds = engine.debugPipeline->getDescriptorSet(currentFrame);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, engine.debugPipeline->getPipelineLayout(), 0, 1, &ds, 0, nullptr);

        DebugPushConstants debugPcs{};
        debugPcs.mipLevel = engine.debugHzbMipLevel;
        debugPcs.nearPlane = engine.cameraNode->getNearPlane();
        debugPcs.farPlane = engine.cameraNode->getFarPlane();
        engine.debugPipeline->pushConstants(cb, debugPcs);

        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRendering(cb);
    }

    // 7. Transition swapchain image layout from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    VkImageMemoryBarrier2 presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    presentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    presentBarrier.dstAccessMask = 0;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.image = context.getSwapChainImages()[imageIndex];
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
