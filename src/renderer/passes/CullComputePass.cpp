#include "CullComputePass.hpp"
#include "core/Engine.hpp"
#include <algorithm>

CullComputePass::CullComputePass(VulkanContext& context, CullPipeline& cullPipeline)
    : context(context), cullPipeline(cullPipeline) {}

void CullComputePass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
    if (engine.gpuScene.totalCullTasks == 0) return;

    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    // Extract Frustum Planes in World Space
    glm::vec4 planes[6];
    planes[0] = glm::vec4(viewProj[0][3] + viewProj[0][0], viewProj[1][3] + viewProj[1][0], viewProj[2][3] + viewProj[2][0], viewProj[3][3] + viewProj[3][0]);
    planes[1] = glm::vec4(viewProj[0][3] - viewProj[0][0], viewProj[1][3] - viewProj[1][0], viewProj[2][3] - viewProj[2][0], viewProj[3][3] - viewProj[3][0]);
    planes[2] = glm::vec4(viewProj[0][3] + viewProj[0][1], viewProj[1][3] + viewProj[1][1], viewProj[2][3] + viewProj[2][1], viewProj[3][3] + viewProj[3][1]);
    planes[3] = glm::vec4(viewProj[0][3] - viewProj[0][1], viewProj[1][3] - viewProj[1][1], viewProj[2][3] - viewProj[2][1], viewProj[3][3] - viewProj[3][1]);
    planes[4] = glm::vec4(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    planes[5] = glm::vec4(viewProj[0][3] - viewProj[0][2], viewProj[1][3] - viewProj[1][2], viewProj[2][3] - viewProj[2][2], viewProj[3][3] - viewProj[3][2]);

    // Normalize plane equations
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }

    glm::vec3 cameraPos = engine.cameraNode->getPosition();

    // Manage frustum freeze state for culling visualization
    static bool wasFrozen = false;
    static CullPushConstants frozenCullPcs{};

    CullPushConstants cullPcs{};

    if (engine.freezeCulling) {
        if (!wasFrozen) {
            frozenCullPcs.viewProj = viewProj;
            for (int i = 0; i < 6; ++i) {
                frozenCullPcs.frustumPlanes[i] = planes[i];
            }
            frozenCullPcs.cameraPos = cameraPos;
            frozenCullPcs.hzbParams = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
            wasFrozen = true;
        }
        cullPcs = frozenCullPcs;
    } else {
        wasFrozen = false;
        cullPcs.viewProj = viewProj;
        for (int i = 0; i < 6; ++i) {
            cullPcs.frustumPlanes[i] = planes[i];
        }
        cullPcs.cameraPos = cameraPos;
        cullPcs.hzbParams = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
    }

    cullPcs.hzbWidth = VulkanContext::HZB_WIDTH;
    cullPcs.hzbHeight = VulkanContext::HZB_HEIGHT;
    cullPcs.maxMipLevel = VulkanContext::HZB_MIP_LEVELS - 1;
    cullPcs.hzbCullingEnabled = engine.hzbCullingEnabled ? 1 : 0;
    cullPcs.lodThreshold = engine.lodThreshold;
    cullPcs.viewportHeight = static_cast<float>(context.getSwapChainExtent().height);
    cullPcs.lodEnabled = engine.lodEnabled ? 1 : 0;
    cullPcs.maxDrawCount = engine.gpuScene.totalCullTasks;
    cullPcs.rasterizerMode = static_cast<uint32_t>(engine.rasterizerMode);
    cullPcs.sizeThreshold = engine.sizeThreshold;
    cullPcs.isInstancedDraw = context.isDrawIndirectCountSupported() ? 0 : 1;
    cullPcs.pad1 = 0.0f;

    // Reset draw count atomic buffer and software draw count atomic buffer
    vkCmdFillBuffer(cb, engine.gpuScene.drawCountBuffer[currentFrame], 0, sizeof(uint32_t), 0);
    vkCmdFillBuffer(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0, sizeof(uint32_t), 0);

    std::vector<VkBufferMemoryBarrier2> fillBarriers;
    fillBarriers.reserve(4);

    VkBufferMemoryBarrier2 barrier0{};
    barrier0.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier0.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier0.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier0.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier0.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier0.buffer = engine.gpuScene.drawCountBuffer[currentFrame];
    barrier0.offset = 0;
    barrier0.size = sizeof(uint32_t);
    fillBarriers.push_back(barrier0);

    VkBufferMemoryBarrier2 barrier1{};
    barrier1.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier1.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier1.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier1.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier1.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier1.buffer = engine.gpuScene.softwareDrawCountBuffer[currentFrame];
    barrier1.offset = 0;
    barrier1.size = sizeof(uint32_t);
    fillBarriers.push_back(barrier1);

    if (!context.isDrawIndirectCountSupported()) {
        // 1. Initialize template command for instanced HW draw fallback
        VkDrawIndirectCommand initCmd{};
        initCmd.vertexCount = 378; // 126 triangles * 3 vertices
        initCmd.instanceCount = 0;
        initCmd.firstVertex = 0;
        initCmd.firstInstance = 0;
        vkCmdUpdateBuffer(cb, engine.gpuScene.culledIndirectBuffer[currentFrame], 0, sizeof(VkDrawIndirectCommand), &initCmd);

        // 2. Clear Software Indirect Buffer (still needed as-is)
        VkDeviceSize size = engine.gpuScene.totalCullTasks * sizeof(VkDrawIndexedIndirectCommand);
        if (size > 0) {
            vkCmdFillBuffer(cb, engine.gpuScene.culledSoftwareIndirectBuffer[currentFrame], 0, size, 0);
        }

        // 3. Barriers for HW Indirect template and SW Indirect buffer
        VkBufferMemoryBarrier2 barrierHWIndirect{};
        barrierHWIndirect.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrierHWIndirect.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrierHWIndirect.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrierHWIndirect.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrierHWIndirect.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        barrierHWIndirect.buffer = engine.gpuScene.culledIndirectBuffer[currentFrame];
        barrierHWIndirect.offset = 0;
        barrierHWIndirect.size = sizeof(VkDrawIndirectCommand);
        fillBarriers.push_back(barrierHWIndirect);

        if (size > 0) {
            VkBufferMemoryBarrier2 barrierSWIndirect{};
            barrierSWIndirect.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrierSWIndirect.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrierSWIndirect.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrierSWIndirect.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrierSWIndirect.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            barrierSWIndirect.buffer = engine.gpuScene.culledSoftwareIndirectBuffer[currentFrame];
            barrierSWIndirect.offset = 0;
            barrierSWIndirect.size = size;
            fillBarriers.push_back(barrierSWIndirect);
        }
    }

    VkDependencyInfo fillDependency{};
    fillDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    fillDependency.bufferMemoryBarrierCount = static_cast<uint32_t>(fillBarriers.size());
    fillDependency.pBufferMemoryBarriers = fillBarriers.data();

    vkCmdPipelineBarrier2(cb, &fillDependency);

    // Bind Cull compute pipeline and global descriptor set
    cullPipeline.bind(cb);

    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        cullPipeline.getPipelineLayout(),
        0,
        1,
        &engine.gpuScene.globalDescriptorSets[currentFrame],
        0,
        nullptr
    );

    cullPipeline.pushConstants(cb, cullPcs);

    uint32_t groupCount = (engine.gpuScene.totalCullTasks + 63) / 64;
    vkCmdDispatch(cb, groupCount, 1, 1);

    if (!context.isDrawIndirectCountSupported()) {
        // Fallback Instanced MDI copy: drawCountBuffer -> culledIndirectBuffer.instanceCount (offset 4)
        VkBufferMemoryBarrier2 copyBarriers[3]{};
        
        // drawCountBuffer: Compute Write -> Transfer Read
        copyBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        copyBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        copyBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        copyBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        copyBarriers[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        copyBarriers[0].buffer = engine.gpuScene.drawCountBuffer[currentFrame];
        copyBarriers[0].offset = 0;
        copyBarriers[0].size = sizeof(uint32_t);

        // culledIndirectBuffer: Compute Access -> Transfer Write
        copyBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        copyBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        copyBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        copyBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        copyBarriers[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        copyBarriers[1].buffer = engine.gpuScene.culledIndirectBuffer[currentFrame];
        copyBarriers[1].offset = 0;
        copyBarriers[1].size = sizeof(VkDrawIndirectCommand);

        // drawnMeshletsBuffer: Compute Write -> Vertex/Compute Shader Read
        copyBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        copyBarriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        copyBarriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        copyBarriers[2].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        copyBarriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        copyBarriers[2].buffer = engine.gpuScene.drawnMeshletsBuffer[currentFrame];
        copyBarriers[2].offset = 0;
        copyBarriers[2].size = engine.gpuScene.totalCullTasks * sizeof(uint32_t);

        VkDependencyInfo copyDependency{};
        copyDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        copyDependency.bufferMemoryBarrierCount = 3;
        copyDependency.pBufferMemoryBarriers = copyBarriers;
        vkCmdPipelineBarrier2(cb, &copyDependency);

        // Copy dynamic count to instanceCount field of the draw template
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offsetof(VkDrawIndirectCommand, instanceCount);
        copyRegion.size = sizeof(uint32_t);
        vkCmdCopyBuffer(cb, engine.gpuScene.drawCountBuffer[currentFrame], engine.gpuScene.culledIndirectBuffer[currentFrame], 1, &copyRegion);

        // Transition culledIndirectBuffer to indirect read stage
        VkBufferMemoryBarrier2 drawBarrier{};
        drawBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        drawBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        drawBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        drawBarrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        drawBarrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        drawBarrier.buffer = engine.gpuScene.culledIndirectBuffer[currentFrame];
        drawBarrier.offset = 0;
        drawBarrier.size = sizeof(VkDrawIndirectCommand);

        VkDependencyInfo drawDependencyInfo{};
        drawDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        drawDependencyInfo.bufferMemoryBarrierCount = 1;
        drawDependencyInfo.pBufferMemoryBarriers = &drawBarrier;
        vkCmdPipelineBarrier2(cb, &drawDependencyInfo);

        // Transition software buffers (Dispatch Indirect)
        VkBufferMemoryBarrier2 swBarriers[2]{};
        
        swBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        swBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        swBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        swBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        swBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        swBarriers[0].buffer = engine.gpuScene.culledSoftwareIndirectBuffer[currentFrame];
        swBarriers[0].offset = 0;
        swBarriers[0].size = sizeof(VkDrawIndexedIndirectCommand) * engine.gpuScene.totalCullTasks;

        swBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        swBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        swBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        swBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        swBarriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        swBarriers[1].buffer = engine.gpuScene.softwareDrawCountBuffer[currentFrame];
        swBarriers[1].offset = 0;
        swBarriers[1].size = sizeof(uint32_t) * 3;

        VkDependencyInfo swDependency{};
        swDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        swDependency.bufferMemoryBarrierCount = 2;
        swDependency.pBufferMemoryBarriers = swBarriers;
        vkCmdPipelineBarrier2(cb, &swDependency);
    } else {
        // Native path: Standard multi-draw indirect barriers
        VkBufferMemoryBarrier2 syncBarriers[4]{};
        
        syncBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        syncBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        syncBarriers[0].buffer = engine.gpuScene.culledIndirectBuffer[currentFrame];
        syncBarriers[0].offset = 0;
        syncBarriers[0].size = sizeof(VkDrawIndexedIndirectCommand) * engine.gpuScene.totalCullTasks;

        syncBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
        syncBarriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
        syncBarriers[1].buffer = engine.gpuScene.drawCountBuffer[currentFrame];
        syncBarriers[1].offset = 0;
        syncBarriers[1].size = sizeof(uint32_t);

        syncBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[2].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        syncBarriers[2].buffer = engine.gpuScene.culledSoftwareIndirectBuffer[currentFrame];
        syncBarriers[2].offset = 0;
        syncBarriers[2].size = sizeof(VkDrawIndexedIndirectCommand) * engine.gpuScene.totalCullTasks;

        syncBarriers[3].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        syncBarriers[3].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        syncBarriers[3].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        syncBarriers[3].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        syncBarriers[3].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        syncBarriers[3].buffer = engine.gpuScene.softwareDrawCountBuffer[currentFrame];
        syncBarriers[3].offset = 0;
        syncBarriers[3].size = sizeof(uint32_t) * 3;

        VkDependencyInfo drawDependency{};
        drawDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        drawDependency.bufferMemoryBarrierCount = 4;
        drawDependency.pBufferMemoryBarriers = syncBarriers;
        vkCmdPipelineBarrier2(cb, &drawDependency);
    }
}

