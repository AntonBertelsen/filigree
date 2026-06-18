#include "HzbDownsamplePass.hpp"
#include "core/Engine.hpp"
#include <stdexcept>
#include <algorithm>

HzbDownsamplePass::HzbDownsamplePass(VulkanContext& context, HzbPipeline& hzbPipeline)
    : context(context), hzbPipeline(hzbPipeline) {
    createHzbResources();
    updateDescriptorSets();
}

HzbDownsamplePass::~HzbDownsamplePass() {
    cleanupHzbResources();
}

void HzbDownsamplePass::createHzbResources() {
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    for (int f = 0; f < 2; f++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = VulkanContext::HZB_WIDTH;
        imageInfo.extent.height = VulkanContext::HZB_HEIGHT;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = VulkanContext::HZB_MIP_LEVELS;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R32_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &hzbImages[f], &hzbImageAllocations[f], nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HZB image!");
        }

        // Create main image view (covers all mip levels, used for sampling)
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = hzbImages[f];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = VulkanContext::HZB_MIP_LEVELS;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &hzbImageViews[f]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HZB main image view!");
        }

        // Create individual level image views (each covers a single mip level, used as storage images)
        for (uint32_t level = 0; level < VulkanContext::HZB_MIP_LEVELS; level++) {
            VkImageViewCreateInfo levelViewInfo{};
            levelViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            levelViewInfo.image = hzbImages[f];
            levelViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            levelViewInfo.format = VK_FORMAT_R32_SFLOAT;
            levelViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            levelViewInfo.subresourceRange.baseMipLevel = level;
            levelViewInfo.subresourceRange.levelCount = 1;
            levelViewInfo.subresourceRange.baseArrayLayer = 0;
            levelViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &levelViewInfo, nullptr, &hzbLevelImageViews[f][level]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create HZB level image view!");
            }
        }

        // Initialize HZB image contents to 1.0 (clear value)
        VkCommandBuffer cb = context.beginSingleTimeCommands();
        
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = hzbImages[f];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VulkanContext::HZB_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cb, &dep);

        VkClearColorValue clearVal{};
        clearVal.float32[0] = 1.0f;
        clearVal.float32[1] = 1.0f;
        clearVal.float32[2] = 1.0f;
        clearVal.float32[3] = 1.0f;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = VulkanContext::HZB_MIP_LEVELS;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        vkCmdClearColorImage(cb, hzbImages[f], VK_IMAGE_LAYOUT_GENERAL, &clearVal, 1, &range);

        // Transition to SHADER_READ_ONLY_OPTIMAL for first culling pass
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cb, &dep);

        context.endSingleTimeCommands(cb);
    }
}

void HzbDownsamplePass::cleanupHzbResources() {
    VkDevice device = context.getDevice();
    VmaAllocator allocator = context.getAllocator();

    for (int f = 0; f < 2; f++) {
        for (uint32_t level = 0; level < VulkanContext::HZB_MIP_LEVELS; level++) {
            if (hzbLevelImageViews[f][level] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, hzbLevelImageViews[f][level], nullptr);
                hzbLevelImageViews[f][level] = VK_NULL_HANDLE;
            }
        }
        if (hzbImageViews[f] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, hzbImageViews[f], nullptr);
            hzbImageViews[f] = VK_NULL_HANDLE;
        }
        if (hzbImages[f] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, hzbImages[f], hzbImageAllocations[f]);
            hzbImages[f] = VK_NULL_HANDLE;
            hzbImageAllocations[f] = VK_NULL_HANDLE;
        }
    }
}

void HzbDownsamplePass::record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) {
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
    hzbToGeneralBarrier.image = hzbImages[currentFrame];
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
        hzbPipeline.recordDispatch(cb, currentFrame, 0, swapExtent.width, swapExtent.height, -1);

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
            lvlBarrier.image = hzbImages[currentFrame];
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
            hzbPipeline.recordDispatch(cb, currentFrame, L, srcWidth, srcHeight, L - 1);
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
    hzbToReadOnlyBarrier.image = hzbImages[currentFrame];
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
}

void HzbDownsamplePass::updateDescriptorSets() {
    std::array<std::array<VkImageView, 11>, 2> levelViews;
    for (int f = 0; f < 2; f++) {
        for (int l = 0; l < 11; l++) {
            levelViews[f][l] = hzbLevelImageViews[f][l];
        }
    }
    hzbPipeline.updateDescriptorSets(levelViews);
}
