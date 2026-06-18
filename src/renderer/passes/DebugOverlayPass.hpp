#pragma once
#include "renderer/passes/IRenderPass.hpp"
#include "renderer/pipelines/BoundsPipeline.hpp"
#include "renderer/pipelines/DebugPipeline.hpp"

class HzbDownsamplePass;

class DebugOverlayPass : public IRenderPass {
public:
    DebugOverlayPass(
        VulkanContext& context, 
        BoundsPipeline& boundsPipeline, 
        DebugPipeline& debugPipeline, 
        HzbDownsamplePass& hzbPass
    );
    ~DebugOverlayPass() override = default;

    void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) override;

private:
    VulkanContext& context;
    BoundsPipeline& boundsPipeline;
    DebugPipeline& debugPipeline;
    HzbDownsamplePass& hzbPass;
};
