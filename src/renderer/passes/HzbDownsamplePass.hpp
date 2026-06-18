#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/HzbPipeline.hpp"

class HzbDownsamplePass : public IRenderPass {
public:
    HzbDownsamplePass(VulkanContext& context, HzbPipeline& hzbPipeline);
    ~HzbDownsamplePass() override;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;

    VkImageView getHzbImageView(uint32_t frameIndex) const { return hzbImageViews[frameIndex]; }
    VkImageView getHzbLevelImageView(uint32_t frameIndex, uint32_t level) const { return hzbLevelImageViews[frameIndex][level]; }

    void updateDescriptorSets();

private:
    void createHzbResources();
    void cleanupHzbResources();

    VulkanContext& context;
    HzbPipeline& hzbPipeline;

    // HZB handles (double buffered)
    VkImage hzbImages[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation hzbImageAllocations[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView hzbImageViews[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView hzbLevelImageViews[2][11] = {};
};
