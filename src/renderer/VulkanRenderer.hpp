#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/pipelines/StandardPipeline.hpp"
#include "renderer/pipelines/CullPipeline.hpp"
#include "renderer/pipelines/BoundsPipeline.hpp"
#include "renderer/pipelines/VisBufferPipeline.hpp"
#include "renderer/pipelines/ResolvePipeline.hpp"
#include <memory>

class Engine; // Forward declaration

class VulkanRenderer {
public:
    VulkanRenderer(VulkanContext& context, StandardPipeline& pipeline, CullPipeline& cullPipeline);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(Engine& engine);
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, Engine& engine);

    void recreateVisBuffer();

private:
    void createVisBufferResources();
    void destroyVisBufferResources();

    VulkanContext& context;
    StandardPipeline& pipeline;
    CullPipeline& cullPipeline;
    std::unique_ptr<BoundsPipeline> boundsPipeline;
    std::unique_ptr<VisBufferPipeline> visBufferPipeline;
    std::unique_ptr<ResolvePipeline> resolvePipeline;

    // VisBuffer resources
    VkImage visBufferImage = VK_NULL_HANDLE;
    VmaAllocation visBufferAllocation = VK_NULL_HANDLE;
    VkImageView visBufferImageView = VK_NULL_HANDLE;
    VkSampler visBufferSampler = VK_NULL_HANDLE;
};
