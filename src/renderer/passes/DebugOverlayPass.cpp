#include "DebugOverlayPass.hpp"
#include "HzbDownsamplePass.hpp"
#include "core/Engine.hpp"

DebugOverlayPass::DebugOverlayPass(
    VulkanContext& context, 
    BoundsPipeline& boundsPipeline, 
    DebugPipeline& debugPipeline, 
    HzbDownsamplePass& hzbPass
) : context(context), boundsPipeline(boundsPipeline), debugPipeline(debugPipeline), hzbPass(hzbPass) {}

void DebugOverlayPass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    // 1. Record Bounding Spheres Debug Pass (Inside swapchain dynamic rendering, on top of forward/deferred)
    if (engine.drawBoundingSpheres && engine.gpuScene.totalCullTasks > 0) {
        VkRenderingAttachmentInfo swapColorAttachment{};
        swapColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
        swapColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve swapchain contents
        swapColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingAttachmentInfo swapDepthAttachment{};
        swapDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapDepthAttachment.imageView = context.getDepthImageView();
        swapDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        swapDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve depth contents
        swapDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo swapRenderingInfo{};
        swapRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        swapRenderingInfo.renderArea.offset = {0, 0};
        swapRenderingInfo.renderArea.extent = context.getSwapChainExtent();
        swapRenderingInfo.layerCount = 1;
        swapRenderingInfo.colorAttachmentCount = 1;
        swapRenderingInfo.pColorAttachments = &swapColorAttachment;
        swapRenderingInfo.pDepthAttachment = &swapDepthAttachment;

        vkCmdBeginRendering(cb, &swapRenderingInfo);

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

        boundsPipeline.bind(cb);

        BoundsPipeline::BoundsPushConstants boundsPcs{};
        boundsPcs.viewProj = viewProj;
        boundsPipeline.pushConstants(cb, boundsPcs);

        vkCmdBindDescriptorSets(
            cb,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            boundsPipeline.getPipelineLayout(),
            0,
            1,
            &engine.gpuScene.globalDescriptorSets[currentFrame],
            0,
            nullptr
        );

        // Draw 3 circles * 16 segments * 2 vertices = 96 vertices per task
        vkCmdDraw(cb, 96, engine.gpuScene.totalCullTasks, 0, 0);

        vkCmdEndRendering(cb);
    }

    // 2. Record HZB Debug Visualizer if enabled
    if (engine.debugVisualiseHzb) {
        VkRenderingAttachmentInfo debugColorAttachment{};
        debugColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        debugColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
        debugColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        debugColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear color since HZB debug is fullscreen
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

        debugPipeline.updateDescriptorSets(
            hzbPass.getHzbImageView(0),
            hzbPass.getHzbImageView(1)
        );

        debugPipeline.bind(cb);

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

        VkDescriptorSet ds = debugPipeline.getDescriptorSet(currentFrame);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline.getPipelineLayout(), 0, 1, &ds, 0, nullptr);

        DebugPushConstants debugPcs{};
        debugPcs.mipLevel = engine.debugHzbMipLevel;
        debugPcs.nearPlane = engine.cameraNode->getNearPlane();
        debugPcs.farPlane = engine.cameraNode->getFarPlane();
        debugPipeline.pushConstants(cb, debugPcs);

        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRendering(cb);
    }
}
