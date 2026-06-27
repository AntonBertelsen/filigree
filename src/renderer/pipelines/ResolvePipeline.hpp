#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class ResolvePipeline {
public:
    ResolvePipeline(VulkanContext& context, VkDescriptorSetLayout globalLayout);
    ~ResolvePipeline();

    // Prevent copying
    ResolvePipeline(const ResolvePipeline&) = delete;
    ResolvePipeline& operator=(const ResolvePipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getGraphicsPipeline(bool use64Bit) const { 
        return use64Bit ? graphicsPipeline64 : graphicsPipeline32; 
    }

    struct ResolvePushConstants {
        glm::mat4 viewProj;
        glm::vec2 viewportSize;
        uint32_t isNaniteMode;
        uint32_t debugMode;
    };

    void bind(VkCommandBuffer cb, bool use64Bit);
    void pushConstants(VkCommandBuffer cb, const ResolvePushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout globalLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline64 = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline32 = VK_NULL_HANDLE;
};
