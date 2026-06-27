#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/VisBufferPipeline.hpp"
#include "renderer/GpuTimestampPool.hpp"

class VisBufferPass : public IRenderPass {
public:
    VisBufferPass(VulkanContext& context, VisBufferPipeline& visBufferPipeline, VkDescriptorSetLayout descriptorSetLayout);
    ~VisBufferPass() override;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;
    void resize(uint32_t width, uint32_t height) override;

    void setTimestampPool(GpuTimestampPool* pool) { timestampPool = pool; }

    VkBuffer getVisBufferSSBO() const { return visBufferSSBO; }
    VkBuffer getDepthBufferSSBO() const { return depthBufferSSBO; }

private:
    void createVisBufferResources(uint32_t width, uint32_t height);
    void destroyVisBufferResources();
    void createSoftwareRasterizerPipeline(VkDescriptorSetLayout layout);

    VulkanContext& context;
    VisBufferPipeline& visBufferPipeline;

    GpuTimestampPool* timestampPool = nullptr; // non-owning, set after construction

    VkBuffer visBufferSSBO = VK_NULL_HANDLE;
    VmaAllocation visBufferSSBOAllocation = VK_NULL_HANDLE;

    VkBuffer depthBufferSSBO = VK_NULL_HANDLE;
    VmaAllocation depthBufferSSBOAllocation = VK_NULL_HANDLE;

    VkPipelineLayout softwarePipelineLayout = VK_NULL_HANDLE;
    VkPipeline softwareComputePipeline64 = VK_NULL_HANDLE;
    VkPipeline softwareComputePipeline32 = VK_NULL_HANDLE;
};
