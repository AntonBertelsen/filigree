#pragma once

#include "core/VulkanContext.hpp"
#include <glm/glm.hpp>

struct DebugPushConstants {
    uint32_t mipLevel;
    float nearPlane;
    float farPlane;
};

class DebugPipeline {
public:
    DebugPipeline(VulkanContext& context);
    ~DebugPipeline();

    DebugPipeline(const DebugPipeline&) = delete;
    DebugPipeline& operator=(const DebugPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const DebugPushConstants& pcs);

    void updateDescriptorSets(VkImageView hzbImageView0, VkImageView hzbImageView1);

private:
    void createPipeline();
    void createDescriptorPoolAndSets();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkSampler hzbSampler = VK_NULL_HANDLE;
};
