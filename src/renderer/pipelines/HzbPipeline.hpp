#pragma once

#include "core/VulkanContext.hpp"
#include <glm/glm.hpp>

#include <array>

struct HzbPushConstants {
    glm::ivec2 srcSize;
    int32_t srcLevel;
};

class HzbPipeline {
public:
    HzbPipeline(VulkanContext& context);
    ~HzbPipeline();

    HzbPipeline(const HzbPipeline&) = delete;
    HzbPipeline& operator=(const HzbPipeline&) = delete;

    VkPipeline getPipeline() const { return computePipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex, uint32_t level) const { return descriptorSets[frameIndex][level]; }

    void recordDispatch(VkCommandBuffer cb, uint32_t frameIndex, uint32_t level, int32_t srcWidth, int32_t srcHeight, int32_t srcLevel);

    void updateDescriptorSets(
        VkBuffer visBufferSSBO,
        const std::array<std::array<VkImageView, 11>, 2>& hzbLevelImageViews
    );

private:
    void createPipeline();
    void createDescriptorPoolAndSets();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets[2][11] = {}; // 2 frames * 11 levels
    VkSampler depthSampler = VK_NULL_HANDLE;
};
