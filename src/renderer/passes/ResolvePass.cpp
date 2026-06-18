#include "ResolvePass.hpp"
#include "VisBufferPass.hpp"
#include "core/Engine.hpp"

ResolvePass::ResolvePass(VulkanContext& context, ResolvePipeline& resolvePipeline, VisBufferPass& visBufferPass)
    : context(context), resolvePipeline(resolvePipeline), visBufferPass(visBufferPass) {}

void ResolvePass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
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

    resolvePipeline.bind(cb);

    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    bool isNanite = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE);

    ResolvePipeline::ResolvePushConstants resolvePcs{};
    resolvePcs.viewProj = viewProj;
    resolvePcs.viewportSize = glm::vec2(viewport.width, viewport.height);
    resolvePcs.isNaniteMode = isNanite ? 1 : 0;
    resolvePcs.debugMode = engine.visBufferDebugMode;
    resolvePipeline.pushConstants(cb, resolvePcs);

    // Bind global descriptor set
    vkCmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        resolvePipeline.getPipelineLayout(),
        0,
        1,
        &engine.gpuScene.globalDescriptorSets[currentFrame],
        0,
        nullptr
    );

    // Draw fullscreen quad (3 vertices, 1 instance)
    vkCmdDraw(cb, 3, 1, 0, 0);
}
