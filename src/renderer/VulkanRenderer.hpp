#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/StandardPipeline.hpp"
#include "renderer/CullPipeline.hpp"

class Engine; // Forward declaration

class VulkanRenderer {
public:
    VulkanRenderer(VulkanContext& context, StandardPipeline& pipeline, CullPipeline& cullPipeline);
    ~VulkanRenderer() = default;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(Engine& engine);
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, Engine& engine);

private:
    VulkanContext& context;
    StandardPipeline& pipeline;
    CullPipeline& cullPipeline;
};
