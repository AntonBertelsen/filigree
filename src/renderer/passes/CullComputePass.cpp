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
    cullPcs.pad0 = 0.0f;
    cullPcs.pad1 = 0.0f;

    // Reset draw count atomic buffer and software draw count atomic buffer
    vkCmdFillBuffer(cb, engine.gpuScene.drawCountBuffer[currentFrame], 0, sizeof(uint32_t), 0);
    vkCmdFillBuffer(cb, engine.gpuScene.softwareDrawCountBuffer[currentFrame], 0, sizeof(uint32_t), 0);

    // Barrier: Wait for dynamic fill buffer to complete before compute shader writes
    VkBufferMemoryBarrier2 fillBarriers[2]{};
    
    fillBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    fillBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    fillBarriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    fillBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    fillBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    fillBarriers[0].buffer = engine.gpuScene.drawCountBuffer[currentFrame];
    fillBarriers[0].offset = 0;
    fillBarriers[0].size = sizeof(uint32_t);

    fillBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    fillBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    fillBarriers[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    fillBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    fillBarriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    fillBarriers[1].buffer = engine.gpuScene.softwareDrawCountBuffer[currentFrame];
    fillBarriers[1].offset = 0;
    fillBarriers[1].size = sizeof(uint32_t);

    VkDependencyInfo fillDependency{};
    fillDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    fillDependency.bufferMemoryBarrierCount = 2;
    fillDependency.pBufferMemoryBarriers = fillBarriers;

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

    // Synchronization Barrier: Wait for compute writes to finish before graphics draws / compute rasterizer reads
    VkBufferMemoryBarrier2 syncBarriers[4]{};
    
    // 1. Hardware Indirect Commands
    syncBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    syncBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    syncBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    syncBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    syncBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    syncBarriers[0].buffer = engine.gpuScene.culledIndirectBuffer[currentFrame];
    syncBarriers[0].offset = 0;
    syncBarriers[0].size = sizeof(VkDrawIndexedIndirectCommand) * engine.gpuScene.totalCullTasks;

    // 2. Hardware Draw Count
    syncBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    syncBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    syncBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    syncBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    syncBarriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    syncBarriers[1].buffer = engine.gpuScene.drawCountBuffer[currentFrame];
    syncBarriers[1].offset = 0;
    syncBarriers[1].size = sizeof(uint32_t);

    // 3. Software Indirect Commands (read as SSBO in software rasterizer compute shader)
    syncBarriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    syncBarriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    syncBarriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    syncBarriers[2].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    syncBarriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    syncBarriers[2].buffer = engine.gpuScene.culledSoftwareIndirectBuffer[currentFrame];
    syncBarriers[2].offset = 0;
    syncBarriers[2].size = sizeof(VkDrawIndexedIndirectCommand) * engine.gpuScene.totalCullTasks;

    // 4. Software Draw Count (read as indirect dispatch parameters in software rasterizer)
    syncBarriers[3].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    syncBarriers[3].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    syncBarriers[3].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    syncBarriers[3].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    syncBarriers[3].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    syncBarriers[3].buffer = engine.gpuScene.softwareDrawCountBuffer[currentFrame];
    syncBarriers[3].offset = 0;
    syncBarriers[3].size = sizeof(uint32_t) * 3; // sizeof(VkDispatchIndirectCommand)

    VkDependencyInfo drawDependency{};
    drawDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    drawDependency.bufferMemoryBarrierCount = 4;
    drawDependency.pBufferMemoryBarriers = syncBarriers;

    vkCmdPipelineBarrier2(cb, &drawDependency);
}
