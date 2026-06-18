#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/ResolvePipeline.hpp"

class VisBufferPass;

class ResolvePass : public IRenderPass {
public:
    ResolvePass(VulkanContext& context, ResolvePipeline& resolvePipeline, VisBufferPass& visBufferPass);
    ~ResolvePass() override = default;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;

private:
    VulkanContext& context;
    ResolvePipeline& resolvePipeline;
    VisBufferPass& visBufferPass;
};
