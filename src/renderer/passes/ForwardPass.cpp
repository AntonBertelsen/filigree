#include "ForwardPass.hpp"
#include "core/Engine.hpp"

ForwardPass::ForwardPass(VulkanContext& context, StandardPipeline& standardPipeline)
    : context(context), standardPipeline(standardPipeline) {}

void ForwardPass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
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

    // Bind Standard Forward pipeline
    standardPipeline.bind(cb);

    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    StandardPipeline::StandardPushConstants standardPcs{};
    standardPcs.viewProj = viewProj;
    standardPcs.isNaniteMode = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) ? 1 : 0;
    standardPipeline.pushConstants(cb, standardPcs);

    // Bind global descriptor set
    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        standardPipeline.getPipelineLayout(),
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

            // Draw via Multi-Draw Indirect Count
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
}
