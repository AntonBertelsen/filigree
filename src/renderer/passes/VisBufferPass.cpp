#include "VisBufferPass.hpp"
#include "core/Engine.hpp"
#include <stdexcept>
#include <iostream>

VisBufferPass::VisBufferPass(VulkanContext& context, VisBufferPipeline& visBufferPipeline, VkDescriptorSetLayout descriptorSetLayout)
    : context(context), visBufferPipeline(visBufferPipeline) {
    VkExtent2D extent = context.getSwapChainExtent();
    createVisBufferResources(extent.width, extent.height);
    createSoftwareRasterizerPipeline(descriptorSetLayout);
}

VisBufferPass::~VisBufferPass() {
    destroyVisBufferResources();
    VkDevice device = context.getDevice();
    if (softwareComputePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, softwareComputePipeline, nullptr);
        softwareComputePipeline = VK_NULL_HANDLE;
    }
    if (softwarePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, softwarePipelineLayout, nullptr);
        softwarePipelineLayout = VK_NULL_HANDLE;
    }
}

void VisBufferPass::resize(uint32_t width, uint32_t height) {
    destroyVisBufferResources();
    createVisBufferResources(width, height);
}

void VisBufferPass::createVisBufferResources(uint32_t width, uint32_t height) {
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint64_t);
    context.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        visBufferSSBO,
        visBufferSSBOAllocation
    );
}

void VisBufferPass::destroyVisBufferResources() {
    VmaAllocator allocator = context.getAllocator();
    if (visBufferSSBO != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, visBufferSSBO, visBufferSSBOAllocation);
        visBufferSSBO = VK_NULL_HANDLE;
        visBufferSSBOAllocation = VK_NULL_HANDLE;
    }
}

void VisBufferPass::createSoftwareRasterizerPipeline(VkDescriptorSetLayout layout) {
    VkDevice device = context.getDevice();

    // 1. Create Pipeline Layout with Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VisBufferPipeline::VisBufferPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &softwarePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Software Rasterizer pipeline layout!");
    }

    // 2. Load compute shader
    auto shaderCode = VulkanContext::readFile(SHADERS_DIR "rasterize.spv");
    VkShaderModule shaderModule = context.createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = softwarePipelineLayout;
    pipelineInfo.stage = shaderStageInfo;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &softwareComputePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Software Rasterizer compute pipeline!");
    }

    vkDestroyShaderModule(device, shaderModule, nullptr);
    std::cout << "Successfully created Software Rasterizer compute pipeline!" << std::endl;
}

void VisBufferPass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
    VkExtent2D extent = context.getSwapChainExtent();
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint64_t);

    // 1. Clear the VisBuffer SSBO using vkCmdFillBuffer
    vkCmdFillBuffer(cb, visBufferSSBO, 0, bufferSize, 0xFFFFFFFFu);

    // 2. Barrier: Transfer Write -> Compute Shader Write/Read (or Fragment Shader if traditional)
    if (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) {
        VkBufferMemoryBarrier2 clearToComputeBarrier{};
        clearToComputeBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        clearToComputeBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        clearToComputeBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        clearToComputeBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        clearToComputeBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        clearToComputeBarrier.buffer = visBufferSSBO;
        clearToComputeBarrier.offset = 0;
        clearToComputeBarrier.size = bufferSize;

        VkDependencyInfo clearDependency{};
        clearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        clearDependency.bufferMemoryBarrierCount = 1;
        clearDependency.pBufferMemoryBarriers = &clearToComputeBarrier;
        vkCmdPipelineBarrier2(cb, &clearDependency);

        // 3. Dispatch Software Rasterizer compute pipeline
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, softwareComputePipeline);

        // Bind global descriptor set
        vkCmdBindDescriptorSets(
            cb,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            softwarePipelineLayout,
            0,
            1,
            &engine.gpuScene.globalDescriptorSets[currentFrame],
            0,
            nullptr
        );

        // Push Constants
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
        glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

        VisBufferPipeline::VisBufferPushConstants visPcs{};
        visPcs.viewProj = viewProj;
        visPcs.isNaniteMode = 1;
        visPcs.viewportWidth = static_cast<float>(extent.width);
        visPcs.viewportHeight = static_cast<float>(extent.height);

        vkCmdPushConstants(
            cb,
            softwarePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(VisBufferPipeline::VisBufferPushConstants),
            &visPcs
        );

        vkCmdDispatchIndirect(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0);

        // 4. Barrier: Compute Rasterizer Write -> Fragment Shader Write (for hardware fallback)
        if (engine.syncMode == Engine::SyncMode::SEQUENTIAL) {
            VkBufferMemoryBarrier2 computeToFragmentBarrier{};
            computeToFragmentBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            computeToFragmentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            computeToFragmentBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            computeToFragmentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            computeToFragmentBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            computeToFragmentBarrier.buffer = visBufferSSBO;
            computeToFragmentBarrier.offset = 0;
            computeToFragmentBarrier.size = bufferSize;

            VkDependencyInfo compToFragDependency{};
            compToFragDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            compToFragDependency.bufferMemoryBarrierCount = 1;
            compToFragDependency.pBufferMemoryBarriers = &computeToFragmentBarrier;
            vkCmdPipelineBarrier2(cb, &compToFragDependency);
        }
    } else {
        // Traditional mode: Transfer Write -> Fragment Shader Write
        VkBufferMemoryBarrier2 clearToFragmentBarrier{};
        clearToFragmentBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        clearToFragmentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        clearToFragmentBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        clearToFragmentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        clearToFragmentBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        clearToFragmentBarrier.buffer = visBufferSSBO;
        clearToFragmentBarrier.offset = 0;
        clearToFragmentBarrier.size = bufferSize;

        VkDependencyInfo clearToFragDependency{};
        clearToFragDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        clearToFragDependency.bufferMemoryBarrierCount = 1;
        clearToFragDependency.pBufferMemoryBarriers = &clearToFragmentBarrier;
        vkCmdPipelineBarrier2(cb, &clearToFragDependency);
    }

    // 5. Hardware Rasterization Graphics Pass
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
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cb, &renderingInfo);

    bool useDepthTested = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE)
        ? (engine.hwPathMode == Engine::HardwarePathMode::DEPTH_TESTED)
        : true;
    visBufferPipeline.bind(cb, useDepthTested);

    // Set Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cb, 0, 1, &scissor);

    // Push Constants
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    VisBufferPipeline::VisBufferPushConstants visPcs{};
    visPcs.viewProj = viewProj;
    visPcs.isNaniteMode = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) ? 1 : 0;
    visPcs.viewportWidth = static_cast<float>(extent.width);
    visPcs.viewportHeight = static_cast<float>(extent.height);
    visBufferPipeline.pushConstants(cb, visPcs);

    // Bind global descriptor set
    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        visBufferPipeline.getPipelineLayout(),
        0,
        1,
        &engine.gpuScene.globalDescriptorSets[currentFrame],
        0,
        nullptr
    );

    if (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) {
        if (engine.gpuScene.totalCullTasks > 0) {
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cb, 0, 1, &engine.gpuScene.vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cb, engine.gpuScene.indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexedIndirectCount(
                cb,
                engine.gpuScene.culledIndirectBuffer[currentFrame],
                0,
                engine.gpuScene.drawCountBuffer[currentFrame],
                0,
                engine.gpuScene.totalCullTasks,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }
    } else {
        if (engine.gpuScene.traditionalIndirectBuffer[currentFrame] != VK_NULL_HANDLE) {
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cb, 0, 1, &engine.gpuScene.vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cb, engine.gpuScene.traditionalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(
                cb,
                engine.gpuScene.traditionalIndirectBuffer[currentFrame],
                0,
                engine.gpuScene.totalInstances,
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }
    }

    vkCmdEndRendering(cb);

    // 6. Barrier: Transition VisBuffer SSBO from Compute/Fragment Write to Compute/Fragment Read (for HZB / Resolve passes)
    VkBufferMemoryBarrier2 readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    readBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    readBarrier.buffer = visBufferSSBO;
    readBarrier.offset = 0;
    readBarrier.size = bufferSize;

    VkDependencyInfo readDependency{};
    readDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    readDependency.bufferMemoryBarrierCount = 1;
    readDependency.pBufferMemoryBarriers = &readBarrier;
    vkCmdPipelineBarrier2(cb, &readDependency);
}
