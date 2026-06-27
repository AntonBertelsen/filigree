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
    VkPipeline getGraphicsPipeline64(bool useDepthTested) const { 
        return useDepthTested ? graphicsPipelineDepthTested64 : graphicsPipelinePureUAV64; 
    }
    VkPipeline getGraphicsPipeline32(bool useDepthTested) const { 
        return useDepthTested ? graphicsPipelineDepthTested32 : graphicsPipelinePureUAV32; 
    }
    VkPipeline getDepthOnlyPipeline() const { return graphicsPipelineDepthOnly; }

    struct VisBufferPushConstants {
        glm::mat4 viewProj;
        uint32_t isNaniteMode;
        float viewportWidth;
        float viewportHeight;
        uint32_t passIndex; // For 32-bit software rasterizer: 0 = depth, 1 = vis payload
    };

    void bind(VkCommandBuffer cb, bool useDepthTested);
    void pushConstants(VkCommandBuffer cb, const VisBufferPushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout cullDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipelinePureUAV64 = VK_NULL_HANDLE;
    VkPipeline graphicsPipelineDepthTested64 = VK_NULL_HANDLE;
    VkPipeline graphicsPipelinePureUAV32 = VK_NULL_HANDLE;
    VkPipeline graphicsPipelineDepthTested32 = VK_NULL_HANDLE;
    VkPipeline graphicsPipelineDepthOnly = VK_NULL_HANDLE;
};
