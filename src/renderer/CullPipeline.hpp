#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct CullPushConstants {
    glm::vec4 frustumPlanes[6];
    glm::vec3 cameraPos;
    uint32_t maxDrawCount;
};

struct MeshletBounds {
    glm::vec4 sphereCenterRadius; // xyz = center, w = radius
    glm::vec4 coneAxisCutoff;     // xyz = axis, w = cutoff
};

class CullPipeline {
public:
    CullPipeline(VulkanContext& context);
    ~CullPipeline();

    CullPipeline(const CullPipeline&) = delete;
    CullPipeline& operator=(const CullPipeline&) = delete;

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getComputePipeline() const { return computePipeline; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    void bind(VkCommandBuffer cb);
    void pushConstants(VkCommandBuffer cb, const CullPushConstants& pcs);

private:
    void createPipeline();
    void cleanup();

    VulkanContext& context;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
};
