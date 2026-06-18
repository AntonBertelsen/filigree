#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/VisBufferPipeline.hpp"

class VisBufferPass : public IRenderPass {
public:
    VisBufferPass(VulkanContext& context, VisBufferPipeline& visBufferPipeline);
    ~VisBufferPass() override;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;
    void resize(uint32_t width, uint32_t height) override;

    VkImageView getVisBufferImageView() const { return visBufferImageView; }
    VkSampler getVisBufferSampler() const { return visBufferSampler; }

private:
    void createVisBufferResources(uint32_t width, uint32_t height);
    void destroyVisBufferResources();

    VulkanContext& context;
    VisBufferPipeline& visBufferPipeline;

    VkImage visBufferImage = VK_NULL_HANDLE;
    VmaAllocation visBufferAllocation = VK_NULL_HANDLE;
    VkImageView visBufferImageView = VK_NULL_HANDLE;
    VkSampler visBufferSampler = VK_NULL_HANDLE;
};
