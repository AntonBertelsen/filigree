#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VisBufferPipeline {
public:
    VisBufferPipeline(VulkanContext& context, VkDescriptorSetLayout cullDescriptorSetLayout);
    ~VisBufferPipeline();

    // Prevent copying
    VisBufferPipeline(const VisBufferPipeline&) = delete;
    VisBufferPipeline& operator=(const VisBufferPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline(bool useDepthTested) const { 
        return useDepthTested ? graphicsPipelineDepthTested : graphicsPipelinePureUAV; 
    }

    struct VisBufferPushConstants {
        glm::mat4 viewProj;
        uint32_t isNaniteMode;
        float viewportWidth;
        float viewportHeight;
    };

    void bind(VkCommandBuffer cb, bool useDepthTested);
    void pushConstants(VkCommandBuffer cb, const VisBufferPushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout cullDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipelinePureUAV = VK_NULL_HANDLE;
    VkPipeline graphicsPipelineDepthTested = VK_NULL_HANDLE;
};
