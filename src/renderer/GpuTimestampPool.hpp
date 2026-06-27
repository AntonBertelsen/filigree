#pragma once

#include "core/VulkanContext.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

// ---------------------------------------------------------------------------
// GpuTimestampPool
// Wraps a VkQueryPool (TIMESTAMP type) to measure GPU time of named passes.
//
// Usage per frame:
//   pool.beginFrame(cb);                      // resets the query pool
//   pool.writeTimestamp(cb, PASS_CULL_START);
//   ... record cull commands ...
//   pool.writeTimestamp(cb, PASS_CULL_END);
//   ... (submit and wait)
//   pool.readback();                           // call AFTER fence signals
//   float ms = pool.elapsedMs(PASS_CULL_START, PASS_CULL_END);
// ---------------------------------------------------------------------------

enum GpuTimestamp : uint32_t {
    TS_FRAME_START = 0,
    TS_CULL_END,
    TS_SW_RASTER_END,   // after software rasterizer compute dispatch
    TS_HW_RASTER_END,   // after hardware rasterizer draw calls
    TS_RESOLVE_END,     // after fullscreen resolve
    TS_HZB_END,
    TS_DEBUG_END,
    TS_FRAME_END,

    TS_COUNT // must be last
};

static const char* kTimestampNames[] = {
    "Frame Start",
    "Cull",
    "SW Rasterizer",
    "HW Rasterizer",
    "Resolve",
    "HZB",
    "Debug Overlay",
    "Frame End",
};

class GpuTimestampPool {
public:
    static constexpr uint32_t FRAMES_IN_FLIGHT = VulkanContext::MAX_FRAMES_IN_FLIGHT;

    GpuTimestampPool() = default;
    ~GpuTimestampPool() { destroy(); }

    GpuTimestampPool(const GpuTimestampPool&) = delete;
    GpuTimestampPool& operator=(const GpuTimestampPool&) = delete;

    // Call once after device is ready
    bool init(VulkanContext& ctx) {
        device = ctx.getDevice();

        // Check timestamp support
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(ctx.getPhysicalDevice(), &props);
        timestampPeriodNs = props.limits.timestampPeriod;

        if (timestampPeriodNs == 0.0f) {
            std::cout << "[GpuTimestampPool] Timestamps not supported on this GPU.\n";
            supported = false;
            return false;
        }

        // Check timestampComputeAndGraphics  
        // (always true for the graphics+compute queue on Vulkan 1.1+)
        supported = true;

        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = TS_COUNT * FRAMES_IN_FLIGHT;

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateQueryPool(device, &info, nullptr, &pools[i]) != VK_SUCCESS) {
                std::cerr << "[GpuTimestampPool] Failed to create query pool.\n";
                supported = false;
                return false;
            }
        }

        // Fill results with 0
        for (auto& r : results) r.fill(0);
        std::cout << "[GpuTimestampPool] Initialized. timestampPeriod = "
                  << timestampPeriodNs << " ns\n";
        return true;
    }

    void destroy() {
        for (auto& p : pools) {
            if (p != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, p, nullptr);
                p = VK_NULL_HANDLE;
            }
        }
    }

    // Call at the start of command buffer recording for this frame
    void beginFrame(VkCommandBuffer cb, uint32_t frameIndex) {
        if (!supported) return;
        currentFrame = frameIndex;
        vkCmdResetQueryPool(cb, pools[frameIndex], 0, TS_COUNT);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             pools[frameIndex], TS_FRAME_START);
    }

    // Write a timestamp at the given slot
    void write(VkCommandBuffer cb, GpuTimestamp ts) {
        if (!supported) return;
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             pools[currentFrame], ts);
    }

    // Call AFTER vkWaitForFences returns (i.e. start of the *next* drawFrame
    // for the same frameIndex, before resetting the command buffer)
    void readback(uint32_t frameIndex) {
        if (!supported) return;
        vkGetQueryPoolResults(
            device, pools[frameIndex],
            0, TS_COUNT,
            sizeof(uint64_t) * TS_COUNT,
            results[frameIndex].data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT // don't use WITH_AVAILABILITY; fence already signaled
        );
    }

    // Return elapsed ms between two timestamps for the last completed frame
    float elapsedMs(GpuTimestamp begin, GpuTimestamp end, uint32_t frameIndex) const {
        if (!supported) return 0.0f;
        uint64_t t0 = results[frameIndex][begin];
        uint64_t t1 = results[frameIndex][end];
        if (t1 < t0) return 0.0f; // wraparound / unwritten slot
        return static_cast<float>(t1 - t0) * timestampPeriodNs * 1e-6f;
    }

    bool isSupported() const { return supported; }

private:
    VkDevice device = VK_NULL_HANDLE;
    float timestampPeriodNs = 0.0f;
    bool supported = false;
    uint32_t currentFrame = 0;

    std::array<VkQueryPool, FRAMES_IN_FLIGHT> pools = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<std::array<uint64_t, TS_COUNT>, FRAMES_IN_FLIGHT> results{};
};
