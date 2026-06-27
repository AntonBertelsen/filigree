#include "VisBufferPass.hpp"
#include "core/Engine.hpp"
#include "renderer/GpuTimestampPool.hpp"
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
    if (softwareComputePipeline64 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, softwareComputePipeline64, nullptr);
        softwareComputePipeline64 = VK_NULL_HANDLE;
    }
    if (softwareComputePipeline32 != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, softwareComputePipeline32, nullptr);
        softwareComputePipeline32 = VK_NULL_HANDLE;
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
    bool use64Bit = context.isShaderInt64AtomicsSupported();
    VkDeviceSize elementSize = use64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
    VkDeviceSize visBufferSize = static_cast<VkDeviceSize>(width) * height * elementSize;
    context.createBuffer(
        visBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        visBufferSSBO,
        visBufferSSBOAllocation
    );

    // Allocate 32-bit depth buffer for two-pass mode (always allocated to avoid empty descriptors)
    VkDeviceSize depthBufferSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint32_t);
    context.createBuffer(
        depthBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        0,
        depthBufferSSBO,
        depthBufferSSBOAllocation
    );
}

void VisBufferPass::destroyVisBufferResources() {
    VmaAllocator allocator = context.getAllocator();
    if (visBufferSSBO != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, visBufferSSBO, visBufferSSBOAllocation);
        visBufferSSBO = VK_NULL_HANDLE;
        visBufferSSBOAllocation = VK_NULL_HANDLE;
    }
    if (depthBufferSSBO != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, depthBufferSSBO, depthBufferSSBOAllocation);
        depthBufferSSBO = VK_NULL_HANDLE;
        depthBufferSSBOAllocation = VK_NULL_HANDLE;
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

    // 2. Load and create 32-bit compute pipeline (always supported)
    {
        auto shaderCode = VulkanContext::readFile(SHADERS_DIR "rasterize_32bit.spv");
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

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &softwareComputePipeline32) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 32-bit Software Rasterizer compute pipeline!");
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
        std::cout << "Successfully created 32-bit Software Rasterizer compute pipeline!" << std::endl;
    }

    // 3. Load and create 64-bit compute pipeline (only if supported)
    if (context.isShaderInt64AtomicsSupported()) {
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

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &softwareComputePipeline64) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create 64-bit Software Rasterizer compute pipeline!");
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
        std::cout << "Successfully created 64-bit Software Rasterizer compute pipeline!" << std::endl;
    } else {
        softwareComputePipeline64 = VK_NULL_HANDLE;
    }
}

void VisBufferPass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
    VkExtent2D extent = context.getSwapChainExtent();
    bool use64Bit = (engine.visBufferMode == Engine::VisBufferMode::SINGLE_PASS_64BIT);
    VkDeviceSize elementSize = use64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * elementSize;

    // 1. Clear the VisBuffer SSBO (and Depth buffer if 32-bit) using vkCmdFillBuffer
    vkCmdFillBuffer(cb, visBufferSSBO, 0, bufferSize, 0xFFFFFFFFu);
    if (!use64Bit) {
        VkDeviceSize depthBufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint32_t);
        vkCmdFillBuffer(cb, depthBufferSSBO, 0, depthBufferSize, 0xFFFFFFFFu);
    }

    // 2. Barrier: Transfer Write -> Compute Shader Write/Read (or Fragment Shader if traditional)
    if (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) {
        std::vector<VkBufferMemoryBarrier2> clearBarriers;
        clearBarriers.reserve(2);

        VkBufferMemoryBarrier2 clearVisBarrier{};
        clearVisBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        clearVisBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        clearVisBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        clearVisBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        clearVisBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        clearVisBarrier.buffer = visBufferSSBO;
        clearVisBarrier.offset = 0;
        clearVisBarrier.size = bufferSize;
        clearBarriers.push_back(clearVisBarrier);

        if (!use64Bit) {
            VkDeviceSize depthBufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint32_t);
            VkBufferMemoryBarrier2 clearDepthBarrier{};
            clearDepthBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            clearDepthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            clearDepthBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            clearDepthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            clearDepthBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            clearDepthBarrier.buffer = depthBufferSSBO;
            clearDepthBarrier.offset = 0;
            clearDepthBarrier.size = depthBufferSize;
            clearBarriers.push_back(clearDepthBarrier);
        }

        VkDependencyInfo clearDependency{};
        clearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        clearDependency.bufferMemoryBarrierCount = static_cast<uint32_t>(clearBarriers.size());
        clearDependency.pBufferMemoryBarriers = clearBarriers.data();
        vkCmdPipelineBarrier2(cb, &clearDependency);

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

        if (use64Bit) {
            // Single-pass 64-bit compute dispatch
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, softwareComputePipeline64);
            visPcs.passIndex = 0;
            vkCmdPushConstants(
                cb,
                softwarePipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(VisBufferPipeline::VisBufferPushConstants),
                &visPcs
            );
            vkCmdDispatchIndirect(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0);
        } else {
            // Two-pass 32-bit compute dispatch
            // Pass 1: Depth write
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, softwareComputePipeline32);
            visPcs.passIndex = 0;
            vkCmdPushConstants(
                cb,
                softwarePipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(VisBufferPipeline::VisBufferPushConstants),
                &visPcs
            );
            vkCmdDispatchIndirect(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0);

            // Barrier: wait for Pass 1 depth writes to finish
            VkBufferMemoryBarrier2 depthBarrier{};
            depthBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            depthBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            depthBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            depthBarrier.buffer = depthBufferSSBO;
            depthBarrier.offset = 0;
            depthBarrier.size = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint32_t);

            VkDependencyInfo depthDependency{};
            depthDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depthDependency.bufferMemoryBarrierCount = 1;
            depthDependency.pBufferMemoryBarriers = &depthBarrier;
            vkCmdPipelineBarrier2(cb, &depthDependency);

            // Pass 2: Visibility payload write
            visPcs.passIndex = 1;
            vkCmdPushConstants(
                cb,
                softwarePipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(VisBufferPipeline::VisBufferPushConstants),
                &visPcs
            );
            vkCmdDispatchIndirect(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0);
        }

        // 4. Barrier: Compute Rasterizer Write -> Fragment Shader Write/Read
        if (engine.syncMode == Engine::SyncMode::SEQUENTIAL) {
            std::vector<VkBufferMemoryBarrier2> computeToFragmentBarriers;
            computeToFragmentBarriers.reserve(2);

            VkBufferMemoryBarrier2 computeToFragmentBarrier{};
            computeToFragmentBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            computeToFragmentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            computeToFragmentBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            computeToFragmentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            computeToFragmentBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            computeToFragmentBarrier.buffer = visBufferSSBO;
            computeToFragmentBarrier.offset = 0;
            computeToFragmentBarrier.size = bufferSize;
            computeToFragmentBarriers.push_back(computeToFragmentBarrier);

            if (!use64Bit) {
                VkDeviceSize depthBufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint32_t);
                VkBufferMemoryBarrier2 depthComputeToFragBarrier{};
                depthComputeToFragBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                depthComputeToFragBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                depthComputeToFragBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                depthComputeToFragBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                depthComputeToFragBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                depthComputeToFragBarrier.buffer = depthBufferSSBO;
                depthComputeToFragBarrier.offset = 0;
                depthComputeToFragBarrier.size = depthBufferSize;
                computeToFragmentBarriers.push_back(depthComputeToFragBarrier);
            }

            VkDependencyInfo compToFragDependency{};
            compToFragDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            compToFragDependency.bufferMemoryBarrierCount = static_cast<uint32_t>(computeToFragmentBarriers.size());
            compToFragDependency.pBufferMemoryBarriers = computeToFragmentBarriers.data();
            vkCmdPipelineBarrier2(cb, &compToFragDependency);
        }
        // Timestamp: end of all software rasterizer work
        if (timestampPool) timestampPool->write(cb, TS_SW_RASTER_END);
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &depthAttachment;

    // Viewport & Scissor setup
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    // Push Constants setup
    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();



    VisBufferPipeline::VisBufferPushConstants visPcs{};
    visPcs.viewProj = viewProj;
    visPcs.isNaniteMode = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) ? 1 : 0;
    visPcs.viewportWidth = static_cast<float>(extent.width);
    visPcs.viewportHeight = static_cast<float>(extent.height);

    auto recordHardwareDraws = [&](VkPipeline pipelineToBind) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToBind);
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        visBufferPipeline.pushConstants(cb, visPcs);

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

                if (context.isDrawIndirectCountSupported()) {
                    vkCmdDrawIndexedIndirectCount(
                        cb,
                        engine.gpuScene.culledIndirectBuffer[currentFrame],
                        0,
                        engine.gpuScene.drawCountBuffer[currentFrame],
                        0,
                        engine.gpuScene.totalCullTasks,
                        sizeof(VkDrawIndexedIndirectCommand)
                    );
                } else {
                    // Fallback indirect draw with optimized limit (no drawIndirectCount support)
                    uint32_t drawCount = engine.gpuScene.totalCullTasks;
                    if (engine.enableDrawCountOptimization) {
                        uint32_t cachedCount = engine.gpuScene.cachedHwDrawCount[currentFrame];
                        if (cachedCount > 0) {
                            // Apply a 30% margin + 256 padding to absorb camera movement, clamped to totalCullTasks
                            drawCount = std::min(engine.gpuScene.totalCullTasks, static_cast<uint32_t>(cachedCount * 1.30f) + 256);
                        }
                    }
                    vkCmdDrawIndexedIndirect(
                        cb,
                        engine.gpuScene.culledIndirectBuffer[currentFrame],
                        0,
                        drawCount,
                        sizeof(VkDrawIndexedIndirectCommand)
                    );
                }



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
    };

    if (use64Bit) {
        // --- 64-bit Single Pass ---
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        vkCmdBeginRendering(cb, &renderingInfo);
        bool useDepthTested = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE)
            ? (engine.hwPathMode == Engine::HardwarePathMode::DEPTH_TESTED)
            : true;
        VkPipeline pipelineToBind = visBufferPipeline.getGraphicsPipeline64(useDepthTested);
        recordHardwareDraws(pipelineToBind);
        vkCmdEndRendering(cb);
        // 64-bit: SW and HW are combined in one pass; no separate SW dispatch
        if (timestampPool) timestampPool->write(cb, TS_SW_RASTER_END);
        if (timestampPool) timestampPool->write(cb, TS_HW_RASTER_END);
    } else {
        // --- 32-bit Two Pass ---
        // Pass 1: Depth-Only rendering
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        vkCmdBeginRendering(cb, &renderingInfo);
        recordHardwareDraws(visBufferPipeline.getDepthOnlyPipeline());
        vkCmdEndRendering(cb);

        // Pass 2: Visibility Payload rendering
        // Load depth values from Pass 1, EQUAL comparison, depth write DISABLED
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

        vkCmdBeginRendering(cb, &renderingInfo);
        bool useDepthTested = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE)
            ? (engine.hwPathMode == Engine::HardwarePathMode::DEPTH_TESTED)
            : true;
        VkPipeline pipelineToBind = visBufferPipeline.getGraphicsPipeline32(useDepthTested);
        recordHardwareDraws(pipelineToBind);
        vkCmdEndRendering(cb);
        if (timestampPool) timestampPool->write(cb, TS_HW_RASTER_END);
    }

    // 6. Barrier: Transition VisBuffer SSBO from Compute/Fragment Write to Compute/Fragment Read (for HZB / Resolve passes)
    std::vector<VkBufferMemoryBarrier2> readBarriers;
    readBarriers.reserve(2);

    VkBufferMemoryBarrier2 readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    readBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    readBarrier.buffer = visBufferSSBO;
    readBarrier.offset = 0;
    readBarrier.size = bufferSize;
    readBarriers.push_back(readBarrier);

    if (!use64Bit) {
        VkDeviceSize depthBufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * sizeof(uint32_t);
        VkBufferMemoryBarrier2 depthReadBarrier{};
        depthReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        depthReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        depthReadBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        depthReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        depthReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        depthReadBarrier.buffer = depthBufferSSBO;
        depthReadBarrier.offset = 0;
        depthReadBarrier.size = depthBufferSize;
        readBarriers.push_back(depthReadBarrier);
    }

    VkDependencyInfo readDependency{};
    readDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    readDependency.bufferMemoryBarrierCount = static_cast<uint32_t>(readBarriers.size());
    readDependency.pBufferMemoryBarriers = readBarriers.data();
    vkCmdPipelineBarrier2(cb, &readDependency);
}
