#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GPUMesh {
    uint32_t baseVertexOffset = 0;
    uint32_t baseIndexOffset = 0;
    uint32_t firstMeshletCommandOffset = 0;
    uint32_t clusterCount = 0;
    uint32_t traditionalIndexCount = 0;
    uint32_t traditionalIndexOffset = 0; // Index in global indices
    uint32_t traditionalVertexOffset = 0; // Vertex in global vertices
};

struct GPUScene {
    // Global static buffers holding all geometry
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    
    VkBuffer indexBuffer = VK_NULL_HANDLE; // Nanite 16-bit indices
    VmaAllocation indexAllocation = VK_NULL_HANDLE;

    VkBuffer traditionalIndexBuffer = VK_NULL_HANDLE; // Traditional 32-bit indices
    VmaAllocation traditionalIndexAllocation = VK_NULL_HANDLE;
    
    VkBuffer boundsBuffer = VK_NULL_HANDLE;
    VmaAllocation boundsAllocation = VK_NULL_HANDLE;
    
    VkBuffer inputCommandsBuffer = VK_NULL_HANDLE;
    VmaAllocation inputCommandsAllocation = VK_NULL_HANDLE;

    // Double-buffered dynamic buffers
    VkBuffer instanceBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation instanceAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkBuffer traditionalIndirectBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation traditionalIndirectAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    VkBuffer culledIndirectBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation culledIndirectAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    VkBuffer drawCountBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation drawCountAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    // Host-visible copy of drawCountBuffer for CPU readback on the fallback (no drawIndirectCount) path
    VkBuffer drawCountReadbackBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation drawCountReadbackAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    // Latched last-frame draw count used by the indirect draw fallback
    uint32_t cachedHwDrawCount[2] = { 0, 0 };
    VkBuffer culledSoftwareIndirectBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation culledSoftwareIndirectAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    VkBuffer softwareDrawCountBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation softwareDrawCountAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    VkBuffer visibilityBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation visibilityAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    // Culling task buffers
    VkBuffer cullTasksBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation cullTasksAllocation[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    
    // Global descriptor sets (Binding 0: inputs, 1: outputs, 2: count, 3: bounds, 4: hzb, 5: visibilities, 6: instances, 7: tasks, 8: vertices, 9: indices, 10: visBufferImg)
    VkDescriptorSet globalDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    uint32_t totalCullTasks = 0;
    uint32_t totalInstances = 0;
    
    VkSampler hzbSampler = VK_NULL_HANDLE;
};

