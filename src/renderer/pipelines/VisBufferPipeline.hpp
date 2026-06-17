#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VisBufferPipeline {
public:
    VisBufferPipeline(VulkanContext& context);
    ~VisBufferPipeline();

    // Prevent copying
    VisBufferPipeline(const VisBufferPipeline&) = delete;
    VisBufferPipeline& operator=(const VisBufferPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }

    struct VisBufferPushConstants {
        glm::mat4 viewProj;
    };

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const VisBufferPushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};
