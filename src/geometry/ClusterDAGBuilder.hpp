#pragma once

#include "scene/MeshNode.hpp"
#include "renderer/pipelines/CullPipeline.hpp"
#include <vector>
#include <cstdint>

struct MeshletData {
    std::vector<MeshVertex> flatVertices;
    std::vector<uint16_t> flatIndices;
    std::vector<VkDrawIndexedIndirectCommand> indirectCommands;
    std::vector<MeshletBounds> boundsList;
    uint32_t clusterCount = 0;

    // Original geometry data for traditional rendering
    std::vector<MeshVertex> originalVertices;
    std::vector<uint32_t> originalIndices;
};

struct DAGCluster {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<uint32_t> origVertexIndices; // Mapping of each local vertex back to original model index
    
    glm::vec4 sphereCenterRadius = glm::vec4(0.0f);
    glm::vec4 coneAxisCutoff = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    glm::vec4 lodSphereCenterRadius = glm::vec4(0.0f);
    glm::vec4 parentLodSphereCenterRadius = glm::vec4(0.0f, 0.0f, 0.0f, 1e10f); // Default to infinity radius for roots
    
    float selfError = 0.0f;
    float parentError = 1e30f; // Default to infinity (for root nodes)
    
    std::vector<uint32_t> childClusterIndices; // Indices in the flat DAG list of its children
};

class ClusterDAGBuilder {
public:
    static MeshletData buildClusterDAG(
        const std::vector<MeshVertex>& vertices, 
        const std::vector<uint32_t>& indices
    );
};
