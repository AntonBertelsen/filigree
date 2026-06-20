#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/VisBufferPipeline.hpp"

class VisBufferPass : public IRenderPass {
public:
    VisBufferPass(VulkanContext& context, VisBufferPipeline& visBufferPipeline, VkDescriptorSetLayout descriptorSetLayout);
    ~VisBufferPass() override;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;
    void resize(uint32_t width, uint32_t height) override;

    VkBuffer getVisBufferSSBO() const { return visBufferSSBO; }

private:
    void createVisBufferResources(uint32_t width, uint32_t height);
    void destroyVisBufferResources();
    void createSoftwareRasterizerPipeline(VkDescriptorSetLayout layout);

    VulkanContext& context;
    VisBufferPipeline& visBufferPipeline;

    VkBuffer visBufferSSBO = VK_NULL_HANDLE;
    VmaAllocation visBufferSSBOAllocation = VK_NULL_HANDLE;

    VkPipelineLayout softwarePipelineLayout = VK_NULL_HANDLE;
    VkPipeline softwareComputePipeline = VK_NULL_HANDLE;
};
