#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class ResolvePipeline {
public:
    ResolvePipeline(VulkanContext& context);
    ~ResolvePipeline();

    // Prevent copying
    ResolvePipeline(const ResolvePipeline&) = delete;
    ResolvePipeline& operator=(const ResolvePipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

    struct ResolvePushConstants {
        glm::mat4 viewProj;
        glm::vec2 viewportSize;
        uint32_t isNaniteMode;
        uint32_t debugMode;
    };

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const ResolvePushConstants& pcs);

    void updateDescriptorSets(
        uint32_t frameIndex,
        VkImageView visBufferImageView,
        VkSampler visBufferSampler,
        VkBuffer vertexBuffer,
        VkBuffer indexBuffer,
        VkBuffer indirectBuffer
    );

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
};
