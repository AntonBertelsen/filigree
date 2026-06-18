#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/CullPipeline.hpp"

class CullComputePass : public IRenderPass {
public:
    CullComputePass(VulkanContext& context, CullPipeline& cullPipeline);
    ~CullComputePass() override = default;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;

private:
    VulkanContext& context;
    CullPipeline& cullPipeline;
};
