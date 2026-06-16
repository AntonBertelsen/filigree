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

    struct StandardPushConstants {
        glm::mat4 viewProj;
        uint32_t isNaniteMode;
    };

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const StandardPushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};
