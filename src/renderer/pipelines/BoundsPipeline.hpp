#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class BoundsPipeline {
public:
    BoundsPipeline(VulkanContext& context, VkDescriptorSetLayout computeDescriptorSetLayout);
    ~BoundsPipeline();

    // Prevent copying
    BoundsPipeline(const BoundsPipeline&) = delete;
    BoundsPipeline& operator=(const BoundsPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const glm::mat4& viewProj);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};
