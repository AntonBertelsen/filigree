#pragma once

#include "scene/MeshNode.hpp"
#include "renderer/CullPipeline.hpp"
#include <vulkan/vulkan.h>
#include <vector>

struct MeshletData {
    std::vector<MeshVertex> flatVertices;
    std::vector<uint16_t> flatIndices;
    std::vector<VkDrawIndexedIndirectCommand> indirectCommands;
    std::vector<MeshletBounds> boundsList;
    uint32_t clusterCount = 0;
};

class MeshletBuilder {
public:
    static MeshletData buildMeshlets(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
};
