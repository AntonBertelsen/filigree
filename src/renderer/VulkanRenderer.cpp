#include "VulkanRenderer.hpp"
#include "core/Engine.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "renderer/pipelines/HzbPipeline.hpp"
#include "renderer/pipelines/DebugPipeline.hpp"
#include "renderer/pipelines/VisBufferPipeline.hpp"
#include "renderer/pipelines/ResolvePipeline.hpp"
#include "renderer/pipelines/BoundsPipeline.hpp"
#include "renderer/passes/CullComputePass.hpp"
#include "renderer/passes/HzbDownsamplePass.hpp"
#include "renderer/passes/VisBufferPass.hpp"
#include "renderer/passes/ResolvePass.hpp"
#include "renderer/passes/ForwardPass.hpp"
#include "renderer/passes/DebugOverlayPass.hpp"
#include <stdexcept>
#include <array>

VulkanRenderer::VulkanRenderer(
    VulkanContext& context, 
    StandardPipeline& pipeline, 
    CullPipeline& cullPipeline,
    HzbPipeline& hzbPipeline,
    DebugPipeline& debugPipeline
) : context(context), pipeline(pipeline), cullPipeline(cullPipeline) {
    boundsPipeline = std::make_unique<BoundsPipeline>(context, cullPipeline.getDescriptorSetLayout());
    visBufferPipeline = std::make_unique<VisBufferPipeline>(context, cullPipeline.getDescriptorSetLayout());
    resolvePipeline = std::make_unique<ResolvePipeline>(context, cullPipeline.getDescriptorSetLayout());

    // Instantiate modular passes
    cullPass = std::make_unique<CullComputePass>(context, cullPipeline);
    visBufferPass = std::make_unique<VisBufferPass>(context, *visBufferPipeline, cullPipeline.getDescriptorSetLayout());
    hzbPass = std::make_unique<HzbDownsamplePass>(context, hzbPipeline, *visBufferPass);
    resolvePass = std::make_unique<ResolvePass>(context, *resolvePipeline, *visBufferPass);
    forwardPass = std::make_unique<ForwardPass>(context, pipeline);
    debugOverlayPass = std::make_unique<DebugOverlayPass>(context, *boundsPipeline, debugPipeline, *hzbPass);

    initImGui();
    timestampPool.init(context);
    // Give VisBufferPass access to timestamps for internal HW/SW breakdown
    visBufferPass->setTimestampPool(&timestampPool);
}

VulkanRenderer::~VulkanRenderer() {
    cleanupImGui();
}

void VulkanRenderer::recreateVisBuffer() {
    VkExtent2D extent = context.getSwapChainExtent();
    visBufferPass->resize(extent.width, extent.height);
}

void VulkanRenderer::updateHzbDescriptorSets() {
    hzbPass->updateDescriptorSets();
}

VkImageView VulkanRenderer::getHzbImageView(uint32_t frameIndex) const {
    return hzbPass->getHzbImageView(frameIndex);
}

void VulkanRenderer::drawFrame(Engine& engine) {
    VkDevice device = context.getDevice();
    VkQueue graphicsQueue = context.getGraphicsQueue();
    VkQueue presentQueue = context.getPresentQueue();

    VkFence currentFence = context.getCurrentInFlightFence();

    // 1. Wait for previous frame's GPU fence
    vkWaitForFences(device, 1, &currentFence, VK_TRUE, UINT64_MAX);

    // 2. Recreate Swapchain if required (e.g. dynamic resize detected on present)
    uint32_t imageIndex;
    while (true) {
        if (engine.framebufferResized) {
            engine.framebufferResized = false;
            engine.recreateSwapChain();
            currentFence = context.getCurrentInFlightFence();
        }

        VkResult acquireResult = vkAcquireNextImageKHR(
            device, 
            context.getSwapChain(), 
            UINT64_MAX, 
            context.getCurrentImageAvailableSemaphore(), 
            VK_NULL_HANDLE, 
            &imageIndex
        );

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            engine.recreateSwapChain();
            continue;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }
        break;
    }

    // Only reset fence if we are proceeding with submission
    vkResetFences(device, 1, &currentFence);

    // Read back GPU timestamps from the frame we just waited on
    uint32_t frameIdx = context.getCurrentFrameIndex();
    timestampPool.readback(frameIdx);

    // Read back draw count from host-visible buffer for platforms without drawIndirectCount
    if (!context.isDrawIndirectCountSupported()) {
        void* mappedData = nullptr;
        if (vmaMapMemory(context.getAllocator(), engine.gpuScene.drawCountReadbackAllocation[frameIdx], &mappedData) == VK_SUCCESS) {
            uint32_t count = *static_cast<uint32_t*>(mappedData);
            engine.gpuScene.cachedHwDrawCount[frameIdx] = count;
            vmaUnmapMemory(context.getAllocator(), engine.gpuScene.drawCountReadbackAllocation[frameIdx]);
        }
    }


    // 3. Reset command buffer
    VkCommandBuffer cb = context.getCurrentCommandBuffer();
    vkResetCommandBuffer(cb, 0);

    // 4. Record commands
    recordCommandBuffer(cb, imageIndex, engine);

    // 5. Submit command buffer to queue
    VkCommandBufferSubmitInfo cbSubmitInfo{};
    cbSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSubmitInfo.commandBuffer = cb;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = context.getCurrentImageAvailableSemaphore();
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = context.getRenderFinishedSemaphore(imageIndex);
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cbSubmitInfo;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo, currentFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    // 6. Present image
    VkSwapchainKHR swapChain = context.getSwapChain();
    VkSemaphore renderFinishedSem = context.getRenderFinishedSemaphore(imageIndex);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSem;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || engine.framebufferResized) {
        engine.framebufferResized = false;
        engine.recreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 7. Advance frame index
    context.advanceFrame();
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, Engine& engine) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    uint32_t currentFrame = context.getCurrentFrameIndex();

    // Write GPU timestamp at the very start of this frame
    timestampPool.beginFrame(cb, currentFrame);

    // 1. Transition swapchain image and depth image layouts
    VkImageMemoryBarrier2 barriers[2]{};
    
    // Color Barrier: Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image = context.getSwapChainImages()[imageIndex];
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;

    // Depth Barrier: Transition depth image to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].image = context.getDepthImage();
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 2;
    dependencyInfo.pImageMemoryBarriers = barriers;

    vkCmdPipelineBarrier2(cb, &dependencyInfo);

    // 2. Compute Culling (if Nanite geometry pipeline is enabled)
    if (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) {
        cullPass->record(cb, currentFrame, imageIndex, engine);
    }
    timestampPool.write(cb, TS_CULL_END);

    // 3. Shading Path: FORWARD vs DEFERRED
    if (engine.shadingPath == Engine::ShadingPath::FORWARD) {
        // --- FORWARD SHADING PATH ---
        VkRenderingAttachmentInfo swapColorAttachment{};
        swapColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
        swapColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swapColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swapColorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

        VkRenderingAttachmentInfo swapDepthAttachment{};
        swapDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapDepthAttachment.imageView = context.getDepthImageView();
        swapDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        swapDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swapDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swapDepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo swapRenderingInfo{};
        swapRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        swapRenderingInfo.renderArea.offset = {0, 0};
        swapRenderingInfo.renderArea.extent = context.getSwapChainExtent();
        swapRenderingInfo.layerCount = 1;
        swapRenderingInfo.colorAttachmentCount = 1;
        swapRenderingInfo.pColorAttachments = &swapColorAttachment;
        swapRenderingInfo.pDepthAttachment = &swapDepthAttachment;

        vkCmdBeginRendering(cb, &swapRenderingInfo);

        forwardPass->record(cb, currentFrame, imageIndex, engine);

        vkCmdEndRendering(cb);
        // Forward path has no SW rasterizer; stamp both as the same point
        timestampPool.write(cb, TS_SW_RASTER_END);
        timestampPool.write(cb, TS_HW_RASTER_END);
        timestampPool.write(cb, TS_RESOLVE_END);
    } else {
        // --- DEFERRED SHADING PATH (VisBuffer + Resolve) ---
        visBufferPass->record(cb, currentFrame, imageIndex, engine);

        VkRenderingAttachmentInfo swapColorAttachment{};
        swapColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
        swapColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swapColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swapColorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

        VkRenderingAttachmentInfo swapDepthAttachment{};
        swapDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapDepthAttachment.imageView = context.getDepthImageView();
        swapDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        swapDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        swapDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo swapRenderingInfo{};
        swapRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        swapRenderingInfo.renderArea.offset = {0, 0};
        swapRenderingInfo.renderArea.extent = context.getSwapChainExtent();
        swapRenderingInfo.layerCount = 1;
        swapRenderingInfo.colorAttachmentCount = 1;
        swapRenderingInfo.pColorAttachments = &swapColorAttachment;
        swapRenderingInfo.pDepthAttachment = &swapDepthAttachment;

        vkCmdBeginRendering(cb, &swapRenderingInfo);

        resolvePass->record(cb, currentFrame, imageIndex, engine);

        vkCmdEndRendering(cb);
        timestampPool.write(cb, TS_RESOLVE_END);
    }

    // 4. Downsample depth buffer to HZB
    hzbPass->record(cb, currentFrame, imageIndex, engine);
    timestampPool.write(cb, TS_HZB_END);

    // 5. Draw debug visualizers (bounding spheres, HZB mip overlay)
    debugOverlayPass->record(cb, currentFrame, imageIndex, engine);
    timestampPool.write(cb, TS_DEBUG_END);

    // 5.5. Render ImGui UI on top
    VkRenderingAttachmentInfo imguiColorAttachment{};
    imguiColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    imguiColorAttachment.imageView = context.getSwapChainImageViews()[imageIndex];
    imguiColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imguiColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load previously rendered frame
    imguiColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo imguiRenderingInfo{};
    imguiRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    imguiRenderingInfo.renderArea.offset = {0, 0};
    imguiRenderingInfo.renderArea.extent = context.getSwapChainExtent();
    imguiRenderingInfo.layerCount = 1;
    imguiRenderingInfo.colorAttachmentCount = 1;
    imguiRenderingInfo.pColorAttachments = &imguiColorAttachment;
    imguiRenderingInfo.pDepthAttachment = nullptr;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData && drawData->Valid && drawData->CmdListsCount > 0) {
        vkCmdBeginRendering(cb, &imguiRenderingInfo);
        ImGui_ImplVulkan_RenderDrawData(drawData, cb);
        vkCmdEndRendering(cb);
    }

    // 6. Transition swapchain image layout from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    VkImageMemoryBarrier2 presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    presentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    presentBarrier.dstAccessMask = 0;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.image = context.getSwapChainImages()[imageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo presentDependencyInfo{};
    presentDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDependencyInfo.imageMemoryBarrierCount = 1;
    presentDependencyInfo.pImageMemoryBarriers = &presentBarrier;

    vkCmdPipelineBarrier2(cb, &presentDependencyInfo);

    timestampPool.write(cb, TS_FRAME_END);

    if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

VkBuffer VulkanRenderer::getVisBufferSSBO() const {
    return visBufferPass->getVisBufferSSBO();
}

VkBuffer VulkanRenderer::getDepthBufferSSBO() const {
    return visBufferPass->getDepthBufferSSBO();
}

void VulkanRenderer::initImGui() {
    // 1. Initialize Platform and Renderer Backends
    ImGui_ImplGlfw_InitForVulkan(context.getWindow(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
    initInfo.Instance = context.getInstance();
    initInfo.PhysicalDevice = context.getPhysicalDevice();
    initInfo.Device = context.getDevice();
    initInfo.QueueFamily = context.findQueueFamilies(context.getPhysicalDevice()).graphicsFamily.value();
    initInfo.Queue = context.getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = VK_NULL_HANDLE; // Let backend allocate its own descriptor pool
    initInfo.DescriptorPoolSize = 16;
    initInfo.MinImageCount = static_cast<uint32_t>(context.getSwapChainImages().size());
    initInfo.ImageCount = static_cast<uint32_t>(context.getSwapChainImages().size());
    initInfo.UseDynamicRendering = true;
    
    // Set dynamic rendering color attachment format info under PipelineInfoMain
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    static VkFormat colorFormat;
    colorFormat = context.getSwapChainImageFormat();
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    
    initInfo.Allocator = nullptr;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend!");
    }
}

void VulkanRenderer::cleanupImGui() {
    vkDeviceWaitIdle(context.getDevice());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}
