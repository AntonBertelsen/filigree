#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/StandardPipeline.hpp"

class ForwardPass : public IRenderPass {
public:
    ForwardPass(VulkanContext& context, StandardPipeline& standardPipeline);
    ~ForwardPass() override = default;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;

private:
    VulkanContext& context;
    StandardPipeline& standardPipeline;
};
