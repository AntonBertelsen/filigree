#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class StandardPipeline {
public:
    StandardPipeline(VulkanContext& context);
    ~StandardPipeline();

    // Prevent copying
    StandardPipeline(const StandardPipeline&) = delete;
    StandardPipeline& operator=(const StandardPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const glm::mat4& viewProj);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};
