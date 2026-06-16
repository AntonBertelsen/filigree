#pragma once

#include "core/VulkanContext.hpp"
#include "geometry/GPUMesh.hpp"
#include "geometry/MeshletBuilder.hpp"

class GPUMeshUploader {
public:
    static void uploadMesh(
        VulkanContext& context,
        VkDescriptorPool descriptorPool,
        VkDescriptorSetLayout descriptorLayout,
        const MeshletData& data,
        GPUMesh& outMesh
    );

    static void updateDescriptorSets(
        VulkanContext& context,
        GPUMesh& outMesh
    );
};
