#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/pipelines/StandardPipeline.hpp"
#include "renderer/pipelines/CullPipeline.hpp"
#include <memory>

class Engine; // Forward declaration
class CullComputePass;
class HzbDownsamplePass;
class VisBufferPass;
class ResolvePass;
class ForwardPass;
class DebugOverlayPass;
class BoundsPipeline;
class VisBufferPipeline;
class ResolvePipeline;
class HzbPipeline;
class DebugPipeline;

class VulkanRenderer {
public:
    VulkanRenderer(
        VulkanContext& context, 
        StandardPipeline& pipeline, 
        CullPipeline& cullPipeline,
        HzbPipeline& hzbPipeline,
        DebugPipeline& debugPipeline
    );
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(Engine& engine);
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, Engine& engine);

    void recreateVisBuffer();
    void updateHzbDescriptorSets();

    VkImageView getHzbImageView(uint32_t frameIndex) const;
    VkBuffer getVisBufferSSBO() const;

private:
    VulkanContext& context;
    StandardPipeline& pipeline;
    CullPipeline& cullPipeline;

    std::unique_ptr<BoundsPipeline> boundsPipeline;
    std::unique_ptr<VisBufferPipeline> visBufferPipeline;
    std::unique_ptr<ResolvePipeline> resolvePipeline;

    // Modular Passes
    std::unique_ptr<CullComputePass> cullPass;
    std::unique_ptr<HzbDownsamplePass> hzbPass;
    std::unique_ptr<VisBufferPass> visBufferPass;
    std::unique_ptr<ResolvePass> resolvePass;
    std::unique_ptr<ForwardPass> forwardPass;
    std::unique_ptr<DebugOverlayPass> debugOverlayPass;

    void initImGui();
    void cleanupImGui();
};
