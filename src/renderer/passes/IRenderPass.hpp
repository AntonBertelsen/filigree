#pragma once
#include <vulkan/vulkan.h>
#include "core/VulkanContext.hpp"
#include "core/Engine.hpp"

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void record(VkCommandBuffer cb, uint32_t currentFrame, uint32_t imageIndex, Engine& engine) = 0;
    virtual void resize(uint32_t width, uint32_t height) {}
};
