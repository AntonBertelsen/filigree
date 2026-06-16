#include "VulkanRenderer.hpp"
#include "core/Engine.hpp"
#include <stdexcept>
#include <array>

VulkanRenderer::VulkanRenderer(VulkanContext& context, StandardPipeline& pipeline, CullPipeline& cullPipeline)
    : context(context), pipeline(pipeline), cullPipeline(cullPipeline) {}

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

    // 2. GPU Compute Pass for Culling (executed outside of rendering pass)
    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 viewProj = engine.cameraNode->getProjectionMatrix(aspect) * engine.cameraNode->getViewMatrix();
    
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
        if (engine.freezeCulling) {
            if (!wasFrozen) {
                for (int i = 0; i < 6; ++i) {
                    engine.frozenFrustumPlanes[i] = planes[i];
                }
                engine.frozenCameraPos = modelSpaceCameraPos;
                wasFrozen = true;
            }
            for (int i = 0; i < 6; ++i) {
                planes[i] = engine.frozenFrustumPlanes[i];
            }
            modelSpaceCameraPos = engine.frozenCameraPos;
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
        cullPipeline.bind(cb);

        CullPushConstants cullPcs{};
        for (int i = 0; i < 6; ++i) {
            cullPcs.frustumPlanes[i] = planes[i];
        }
        cullPcs.cameraPos = modelSpaceCameraPos;
        cullPcs.maxDrawCount = mesh.clusterCount;

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
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} }; // Clear: Black

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = context.getDepthImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 }; // Clear to 1.0 (far plane)

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = context.getSwapChainExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cb, &renderingInfo);

    // 4. Bind the Graphics Pipeline
    pipeline.bind(cb);

    // 5. Set Dynamic Viewport & Scissor
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

    // 6. Push Combined MVP Matrix
    glm::mat4 mvp = viewProj * modelMatrix;
    pipeline.pushConstants(cb, mvp);

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
