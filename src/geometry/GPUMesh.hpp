#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GPUMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
    
    // Original CPU-uploaded MDI buffer (used as compute input)
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectAllocation = VK_NULL_HANDLE;
    
    // Double-buffered dynamic culled MDI buffers (compute output, rasterizer input)
    VkBuffer culledIndirectBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation culledIndirectAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    // Double-buffered draw counts (compute output, MDI count input)
    VkBuffer drawCountBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation drawCountAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    // Static meshlet bounding sphere and cone data
    VkBuffer boundsBuffer = VK_NULL_HANDLE;
    VmaAllocation boundsAllocation = VK_NULL_HANDLE;
    
    // Compute descriptor sets (one per frame in flight)
    VkDescriptorSet computeDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkSampler hzbSampler = VK_NULL_HANDLE;
    uint32_t clusterCount = 0;
    uint32_t totalIndexCount = 0;

    // Traditional rendering resources
    VkBuffer traditionalVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation traditionalVertexAllocation = VK_NULL_HANDLE;
    VkBuffer traditionalIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation traditionalIndexAllocation = VK_NULL_HANDLE;
    uint32_t traditionalIndexCount = 0;

    // Double-buffered visibility buffer (debug feedback)
    VkBuffer visibilityBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation visibilityAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
};
