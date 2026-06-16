#include "geometry/MeshletBuilder.hpp"
#include <meshoptimizer.h>
#include <iostream>

MeshletData MeshletBuilder::buildMeshlets(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices) {
    MeshletData data{};
    
    if (vertices.empty() || indices.empty()) {
        return data;
    }
    
    size_t max_vertices = 64;
    size_t max_triangles = 126;
    float cone_weight = 0.0f;
    
    size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
    std::vector<unsigned int> meshlet_vertices(max_meshlets * max_vertices);
    std::vector<unsigned char> meshlet_triangles(max_meshlets * max_triangles * 3);
    
    size_t meshlet_count = meshopt_buildMeshlets(
        meshlets.data(),
        meshlet_vertices.data(),
        meshlet_triangles.data(),
        indices.data(),
        indices.size(),
        &vertices[0].pos.x,
        vertices.size(),
        sizeof(MeshVertex),
        max_vertices,
        max_triangles,
        cone_weight
    );
    
    meshlets.resize(meshlet_count);
    data.clusterCount = static_cast<uint32_t>(meshlet_count);
    
    data.flatVertices.reserve(meshlet_count * max_vertices);
    data.flatIndices.reserve(meshlet_count * max_triangles * 3);
    data.indirectCommands.reserve(meshlet_count);
    data.boundsList.reserve(meshlet_count);
    
    for (size_t i = 0; i < meshlet_count; ++i) {
        const auto& meshlet = meshlets[i];
        
        uint32_t baseVertexOffset = static_cast<uint32_t>(data.flatVertices.size());
        uint32_t baseIndexOffset = static_cast<uint32_t>(data.flatIndices.size());
        
        // 1. Copy meshlet vertices
        for (uint32_t v = 0; v < meshlet.vertex_count; ++v) {
            uint32_t origVertexIdx = meshlet_vertices[meshlet.vertex_offset + v];
            MeshVertex vertex = vertices[origVertexIdx];
            data.flatVertices.push_back(vertex);
        }
        
        // 2. Copy meshlet indices (local uint16_t)
        for (uint32_t t = 0; t < meshlet.triangle_count; ++t) {
            uint16_t localIdx0 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 0];
            uint16_t localIdx1 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 1];
            uint16_t localIdx2 = meshlet_triangles[meshlet.triangle_offset + t * 3 + 2];
            
            data.flatIndices.push_back(localIdx0);
            data.flatIndices.push_back(localIdx1);
            data.flatIndices.push_back(localIdx2);
        }
        
        // 3. Construct indirect draw command
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = meshlet.triangle_count * 3;
        cmd.instanceCount = 1;
        cmd.firstIndex = baseIndexOffset;
        cmd.vertexOffset = static_cast<int32_t>(baseVertexOffset);
        cmd.firstInstance = static_cast<uint32_t>(i);
        
        data.indirectCommands.push_back(cmd);
 
        // 4. Compute bounds (Sphere & Cone)
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshlet_vertices[meshlet.vertex_offset],
            &meshlet_triangles[meshlet.triangle_offset],
            meshlet.triangle_count,
            &vertices[0].pos.x,
            vertices.size(),
            sizeof(MeshVertex)
        );
 
        MeshletBounds b{};
        b.sphereCenterRadius = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
        b.coneAxisCutoff = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
        
        data.boundsList.push_back(b);
    }
    
    data.originalVertices = vertices;
    data.originalIndices = indices;
    
    return data;
}
