#include "VisBufferPass.hpp"
#include "core/Engine.hpp"
#include <stdexcept>

VisBufferPass::VisBufferPass(VulkanContext& context, VisBufferPipeline& visBufferPipeline)
    : context(context), visBufferPipeline(visBufferPipeline) {
    VkExtent2D extent = context.getSwapChainExtent();
    createVisBufferResources(extent.width, extent.height);
}

VisBufferPass::~VisBufferPass() {
    destroyVisBufferResources();
}

void VisBufferPass::resize(uint32_t width, uint32_t height) {
    destroyVisBufferResources();
    createVisBufferResources(width, height);
}

void VisBufferPass::createVisBufferResources(uint32_t width, uint32_t height) {
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    // 1. Create Image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
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
        throw std::runtime_error("Failed to create VisBuffer image inside VisBufferPass!");
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
        throw std::runtime_error("Failed to create VisBuffer imageView inside VisBufferPass!");
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
        throw std::runtime_error("Failed to create VisBuffer sampler inside VisBufferPass!");
    }
}

void VisBufferPass::destroyVisBufferResources() {
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

void VisBufferPass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
    // Image Barrier: Transition VisBuffer to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2 visBarrier{};
    visBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    visBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    visBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    visBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    visBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    visBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

    visBufferPipeline.bind(cb);

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
    float aspect = static_cast<float>(context.getSwapChainExtent().width) / static_cast<float>(context.getSwapChainExtent().height);
    glm::mat4 proj = engine.cameraNode->getProjectionMatrix(aspect);
    glm::mat4 viewProj = proj * engine.cameraNode->getViewMatrix();

    VisBufferPipeline::VisBufferPushConstants visPcs{};
    visPcs.viewProj = viewProj;
    visPcs.isNaniteMode = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) ? 1 : 0;
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
