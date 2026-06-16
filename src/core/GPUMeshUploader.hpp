#pragma once

#include "core/VulkanContext.hpp"
#include "core/GPUMesh.hpp"
#include "renderer/MeshletBuilder.hpp"

class GPUMeshUploader {
public:
    static void uploadMesh(
        VulkanContext& context,
        VkDescriptorPool descriptorPool,
        VkDescriptorSetLayout descriptorLayout,
        const MeshletData& data,
        GPUMesh& outMesh
    );
};
