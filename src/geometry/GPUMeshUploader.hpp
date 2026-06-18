#pragma once

#include "core/VulkanContext.hpp"
#include "geometry/GPUMesh.hpp"
#include "geometry/ClusterDAGBuilder.hpp"
#include <vector>
#include <glm/glm.hpp>

struct InstanceData {
    glm::mat4 modelMatrix;
    uint32_t baseVertexOffset;
    uint32_t baseIndexOffset;
    uint32_t firstMeshletCommandOffset;
    uint32_t isNanite;
};

struct CullTask {
    uint32_t globalInstIdx;
    uint32_t clustIdx;
};

class GPUMeshUploader {
public:
    static void uploadScene(
        VulkanContext& context,
        VkDescriptorPool descriptorPool,
        VkDescriptorSetLayout globalLayout,
        const std::vector<MeshletData>& meshletDatas,
        GPUScene& outScene,
        std::vector<GPUMesh>& outMeshes
    );

    static void uploadSceneInstances(
        VulkanContext& context,
        GPUScene& scene,
        const std::vector<GPUMesh>& meshes,
        const std::vector<std::vector<glm::mat4>>& modelInstances
    );

    static void updateDescriptorSets(
        VulkanContext& context,
        GPUScene& scene,
        VkImageView hzbImageView0,
        VkImageView hzbImageView1,
        VkImageView visBufferImageView,
        VkSampler visBufferSampler
    );
};
