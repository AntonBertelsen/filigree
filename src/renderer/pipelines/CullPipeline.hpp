#pragma once

#include "core/VulkanContext.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct CullPushConstants {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    glm::vec3 cameraPos;
    float padding; // Align hzbParams to 16-byte boundary
    glm::vec4 hzbParams; // x = proj[0][0], y = proj[1][1], z = proj[2][2], w = proj[3][2]
    uint32_t maxDrawCount;
    uint32_t hzbWidth;
    uint32_t hzbHeight;
    uint32_t maxMipLevel;
    uint32_t hzbCullingEnabled;
    float lodThreshold;
    float viewportHeight;
    uint32_t lodEnabled;
};

struct MeshletBounds {
    glm::vec4 sphereCenterRadius;          // xyz = center, w = radius (individual bounds)
    glm::vec4 coneAxisCutoff;              // xyz = axis, w = cutoff
    glm::vec4 lodSphereCenterRadius;       // xyz = center, w = radius (LOD group bounds)
    glm::vec4 parentLodSphereCenterRadius; // xyz = center, w = radius (LOD parent group bounds)
    glm::vec4 lodParams;                   // x = selfError, y = parentError, zw = padding
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
