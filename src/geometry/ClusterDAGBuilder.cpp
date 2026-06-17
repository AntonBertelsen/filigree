#include "geometry/ClusterDAGBuilder.hpp"
#include <meshoptimizer.h>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <algorithm>

static glm::vec4 unionSpheres(const std::vector<glm::vec4>& spheres) {
    if (spheres.empty()) return glm::vec4(0.0f);
    if (spheres.size() == 1) return spheres[0];
    
    // Compute average center
    glm::vec3 center(0.0f);
    for (const auto& s : spheres) {
        center += glm::vec3(s);
    }
    center /= static_cast<float>(spheres.size());
    
    // Compute radius enclosing all spheres
    float radius = 0.0f;
    for (const auto& s : spheres) {
        float dist = glm::distance(center, glm::vec3(s));
        radius = std::max(radius, dist + s.w);
    }
    return glm::vec4(center.x, center.y, center.z, radius);
}

MeshletData ClusterDAGBuilder::buildClusterDAG(
    const std::vector<MeshVertex>& vertices, 
    const std::vector<uint32_t>& indices
) {
    std::vector<DAGCluster> allClusters;
    
    if (vertices.empty() || indices.empty()) {
        return MeshletData{};
    }
    
    // ==========================================
    // 1. Build Initial Level 0 (Leaf Clusters)
    // ==========================================
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
    
    allClusters.reserve(meshlet_count * 2); // Pre-allocate some room for parent levels
    
    for (size_t i = 0; i < meshlet_count; ++i) {
        const auto& meshlet = meshlets[i];
        DAGCluster cluster{};
        
        cluster.vertices.reserve(meshlet.vertex_count);
        cluster.origVertexIndices.reserve(meshlet.vertex_count);
        
        for (uint32_t v = 0; v < meshlet.vertex_count; ++v) {
            uint32_t origVertexIdx = meshlet_vertices[meshlet.vertex_offset + v];
            cluster.vertices.push_back(vertices[origVertexIdx]);
            cluster.origVertexIndices.push_back(origVertexIdx);
        }
        
        cluster.indices.reserve(meshlet.triangle_count * 3);
        for (uint32_t t = 0; t < meshlet.triangle_count; ++t) {
            cluster.indices.push_back(meshlet_triangles[meshlet.triangle_offset + t * 3 + 0]);
            cluster.indices.push_back(meshlet_triangles[meshlet.triangle_offset + t * 3 + 1]);
            cluster.indices.push_back(meshlet_triangles[meshlet.triangle_offset + t * 3 + 2]);
        }
        
        // Compute bounds
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshlet_vertices[meshlet.vertex_offset],
            &meshlet_triangles[meshlet.triangle_offset],
            meshlet.triangle_count,
            &vertices[0].pos.x,
            vertices.size(),
            sizeof(MeshVertex)
        );
        cluster.sphereCenterRadius = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
        cluster.coneAxisCutoff = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
        cluster.lodSphereCenterRadius = cluster.sphereCenterRadius;
        
        cluster.selfError = 0.0f;
        cluster.parentError = 1e30f; // Roots by default
        
        allClusters.push_back(cluster);
    }
    
    std::cout << "[DAG Builder] Level 0 generated: " << meshlet_count << " clusters." << std::endl;
    
    // ==========================================
    // 2. Build DAG Hierarchy
    // ==========================================
    uint32_t currentLevelStart = 0;
    uint32_t currentLevelCount = static_cast<uint32_t>(allClusters.size());
    int level = 1;
    
    while (currentLevelCount > 1) {
        // Step A: Build Adjacency Graph mapping original vertex indices to cluster IDs
        std::unordered_map<uint32_t, std::vector<uint32_t>> vertexToClusters;
        for (uint32_t i = 0; i < currentLevelCount; ++i) {
            uint32_t clusterIdx = currentLevelStart + i;
            for (uint32_t origIdx : allClusters[clusterIdx].origVertexIndices) {
                vertexToClusters[origIdx].push_back(clusterIdx);
            }
        }
        
        std::vector<std::unordered_set<uint32_t>> adjacency(currentLevelCount);
        for (const auto& pair : vertexToClusters) {
            const auto& clusterList = pair.second;
            for (size_t c1 = 0; c1 < clusterList.size(); ++c1) {
                for (size_t c2 = c1 + 1; c2 < clusterList.size(); ++c2) {
                    uint32_t idx1 = clusterList[c1];
                    uint32_t idx2 = clusterList[c2];
                    adjacency[idx1 - currentLevelStart].insert(idx2);
                    adjacency[idx2 - currentLevelStart].insert(idx1);
                }
            }
        }
        
        // Step B: Group Clusters (Greedy partitioning into groups of up to 4)
        std::vector<std::vector<uint32_t>> groups;
        std::vector<bool> visited(currentLevelCount, false);
        
        // Pass 1: Adjacency-based grouping
        for (uint32_t i = 0; i < currentLevelCount; ++i) {
            if (visited[i]) continue;
            
            std::vector<uint32_t> group;
            group.push_back(currentLevelStart + i);
            visited[i] = true;
            
            // Gather unvisited neighbors
            for (uint32_t neighbor : adjacency[i]) {
                uint32_t localNeighbor = neighbor - currentLevelStart;
                if (group.size() < 4 && !visited[localNeighbor]) {
                    group.push_back(neighbor);
                    visited[localNeighbor] = true;
                }
            }
            
            // 2-hop neighbor search if group size is still under 4
            if (group.size() < 4) {
                for (size_t m = 0; m < group.size(); ++m) {
                    uint32_t memberLocal = group[m] - currentLevelStart;
                    for (uint32_t neighbor : adjacency[memberLocal]) {
                        uint32_t localNeighbor = neighbor - currentLevelStart;
                        if (group.size() < 4 && !visited[localNeighbor]) {
                            group.push_back(neighbor);
                            visited[localNeighbor] = true;
                        }
                    }
                    if (group.size() == 4) break;
                }
            }
            
            if (group.size() > 1) {
                groups.push_back(group);
            } else {
                // Reset visited for size-1 groups so they can be grouped in Pass 2
                visited[i] = false;
            }
        }
        
        // Pass 2: Group remaining isolated clusters together using greedy spatial proximity
        std::vector<uint32_t> remaining;
        for (uint32_t i = 0; i < currentLevelCount; ++i) {
            if (!visited[i]) {
                remaining.push_back(currentLevelStart + i);
            }
        }
        
        std::vector<bool> remainingVisited(remaining.size(), false);
        for (size_t i = 0; i < remaining.size(); ++i) {
            if (remainingVisited[i]) continue;
            
            std::vector<uint32_t> group = { remaining[i] };
            remainingVisited[i] = true;
            
            while (group.size() < 4) {
                float minDistance = 1e30f;
                int bestIdx = -1;
                glm::vec3 centerA = glm::vec3(allClusters[group.back()].sphereCenterRadius);
                
                for (size_t j = 0; j < remaining.size(); ++j) {
                    if (!remainingVisited[j]) {
                        glm::vec3 centerB = glm::vec3(allClusters[remaining[j]].sphereCenterRadius);
                        float dist = glm::distance(centerA, centerB);
                        if (dist < minDistance) {
                            minDistance = dist;
                            bestIdx = static_cast<int>(j);
                        }
                    }
                }
                if (bestIdx != -1) {
                    group.push_back(remaining[bestIdx]);
                    remainingVisited[bestIdx] = true;
                } else {
                    break;
                }
            }
            groups.push_back(group);
        }
        
        // Step C: Merge, Simplify, and Split Groups
        std::vector<DAGCluster> nextLevelClusters;
        uint32_t nextLevelStart = static_cast<uint32_t>(allClusters.size());
        
        for (const auto& G : groups) {
            float maxChildError = 0.0f;
            for (uint32_t childIdx : G) {
                maxChildError = std::max(maxChildError, allClusters[childIdx].selfError);
            }
            
            // Compute union bounding sphere of children
            std::vector<glm::vec4> childSpheres;
            childSpheres.reserve(G.size());
            for (uint32_t childIdx : G) {
                childSpheres.push_back(allClusters[childIdx].sphereCenterRadius);
            }
            glm::vec4 groupUnionSphere = unionSpheres(childSpheres);
            
            if (G.size() == 1) {
                // If group has only 1 cluster, we cannot simplify it with neighbors.
                // Just pass it as-is to the next level to maintain tree structure.
                DAGCluster parent = allClusters[G[0]];
                parent.selfError = maxChildError;
                parent.parentError = 1e30f;
                parent.childClusterIndices = { G[0] };
                parent.lodSphereCenterRadius = groupUnionSphere;
                
                nextLevelClusters.push_back(parent);
                
                // Update child's parent error and parent LOD sphere link
                allClusters[G[0]].parentError = maxChildError;
                allClusters[G[0]].parentLodSphereCenterRadius = groupUnionSphere;
                continue;
            }
            
            // Merge vertices and indices of all clusters in the group
            std::vector<MeshVertex> mergedVertices;
            std::vector<uint32_t> mergedIndices;
            std::vector<uint32_t> mergedOrigVertexIndices;
            std::unordered_map<uint32_t, uint32_t> origToMergedIdx;
            
            for (uint32_t childIdx : G) {
                const auto& child = allClusters[childIdx];
                for (uint16_t localIdx : child.indices) {
                    uint32_t origIdx = child.origVertexIndices[localIdx];
                    const auto& vertex = child.vertices[localIdx];
                    
                    auto it = origToMergedIdx.find(origIdx);
                    if (it == origToMergedIdx.end()) {
                        uint32_t newIdx = static_cast<uint32_t>(mergedVertices.size());
                        origToMergedIdx[origIdx] = newIdx;
                        mergedVertices.push_back(vertex);
                        mergedOrigVertexIndices.push_back(origIdx);
                        mergedIndices.push_back(newIdx);
                    } else {
                        mergedIndices.push_back(it->second);
                    }
                }
            }
            
            // Simplify combined mesh
            std::vector<uint32_t> simplifiedIndices(mergedIndices.size());
            float target_error = 1.0f; // Enable aggressive simplification by removing 1% error cap
            float result_error = 0.0f;
            unsigned int options = meshopt_SimplifyLockBorder;
            
            size_t targetIndexCount = mergedIndices.size() / 2;
            if (targetIndexCount < 3) targetIndexCount = 3;
            
            size_t simplifiedIndexCount = meshopt_simplify(
                simplifiedIndices.data(),
                mergedIndices.data(),
                mergedIndices.size(),
                &mergedVertices[0].pos.x,
                mergedVertices.size(),
                sizeof(MeshVertex),
                targetIndexCount,
                target_error,
                options,
                &result_error
            );
            
            float absoluteError = 0.0f;
            if (simplifiedIndexCount == 0 || simplifiedIndexCount > mergedIndices.size()) {
                // Fallback: Simplifier failed or couldn't reduce. Keep original merged mesh.
                simplifiedIndices = mergedIndices;
                simplifiedIndexCount = mergedIndices.size();
                absoluteError = maxChildError + 1e-5f;
            } else {
                simplifiedIndices.resize(simplifiedIndexCount);
                float scale = meshopt_simplifyScale(
                    &mergedVertices[0].pos.x,
                    mergedVertices.size(),
                    sizeof(MeshVertex)
                );
                absoluteError = result_error * scale;
            }
            
            float selfError = std::max(absoluteError, maxChildError + 1e-5f);
            
            // Split simplified mesh back into parent meshlets
            size_t max_parent_meshlets = meshopt_buildMeshletsBound(simplifiedIndices.size(), max_vertices, max_triangles);
            std::vector<meshopt_Meshlet> parent_meshlets(max_parent_meshlets);
            std::vector<unsigned int> parent_meshlet_vertices(max_parent_meshlets * max_vertices);
            std::vector<unsigned char> parent_meshlet_triangles(max_parent_meshlets * max_triangles * 3);
            
            size_t parent_meshlet_count = meshopt_buildMeshlets(
                parent_meshlets.data(),
                parent_meshlet_vertices.data(),
                parent_meshlet_triangles.data(),
                simplifiedIndices.data(),
                simplifiedIndices.size(),
                &mergedVertices[0].pos.x,
                mergedVertices.size(),
                sizeof(MeshVertex),
                max_vertices,
                max_triangles,
                cone_weight
            );
            
            // If parent_meshlet_count is not strictly smaller than children group size,
            // we must force reduction by unlocking the borders or simplifying further.
            if (parent_meshlet_count >= G.size() && G.size() > 1) {
                // Attempt 1: Re-simplify without border lock
                simplifiedIndexCount = meshopt_simplify(
                    simplifiedIndices.data(),
                    mergedIndices.data(),
                    mergedIndices.size(),
                    &mergedVertices[0].pos.x,
                    mergedVertices.size(),
                    sizeof(MeshVertex),
                    targetIndexCount,
                    1.0f,
                    0, // options = 0 (unlock borders)
                    &result_error
                );
                simplifiedIndices.resize(simplifiedIndexCount);
                
                parent_meshlet_count = meshopt_buildMeshlets(
                    parent_meshlets.data(),
                    parent_meshlet_vertices.data(),
                    parent_meshlet_triangles.data(),
                    simplifiedIndices.data(),
                    simplifiedIndices.size(),
                    &mergedVertices[0].pos.x,
                    mergedVertices.size(),
                    sizeof(MeshVertex),
                    max_vertices,
                    max_triangles,
                    cone_weight
                );
                
                // Attempt 2: Aggressive halving if still not reduced
                int attempts = 0;
                while (parent_meshlet_count >= G.size() && targetIndexCount > 3 && attempts < 3) {
                    targetIndexCount /= 2;
                    if (targetIndexCount < 3) targetIndexCount = 3;
                    
                    simplifiedIndexCount = meshopt_simplify(
                        simplifiedIndices.data(),
                        mergedIndices.data(),
                        mergedIndices.size(),
                        &mergedVertices[0].pos.x,
                        mergedVertices.size(),
                        sizeof(MeshVertex),
                        targetIndexCount,
                        1.0f,
                        0, // options = 0
                        &result_error
                    );
                    simplifiedIndices.resize(simplifiedIndexCount);
                    
                    parent_meshlet_count = meshopt_buildMeshlets(
                        parent_meshlets.data(),
                        parent_meshlet_vertices.data(),
                        parent_meshlet_triangles.data(),
                        simplifiedIndices.data(),
                        simplifiedIndices.size(),
                        &mergedVertices[0].pos.x,
                        mergedVertices.size(),
                        sizeof(MeshVertex),
                        max_vertices,
                        max_triangles,
                        cone_weight
                    );
                    attempts++;
                }
            }
            
            parent_meshlets.resize(parent_meshlet_count);
            
            // Update children to point to this parent level error and parent LOD sphere
            for (uint32_t childIdx : G) {
                allClusters[childIdx].parentError = selfError;
                allClusters[childIdx].parentLodSphereCenterRadius = groupUnionSphere;
            }
            
            // Generate DAGCluster parent objects
            for (size_t pm = 0; pm < parent_meshlet_count; ++pm) {
                const auto& meshlet = parent_meshlets[pm];
                DAGCluster parent{};
                
                parent.vertices.reserve(meshlet.vertex_count);
                parent.origVertexIndices.reserve(meshlet.vertex_count);
                
                for (uint32_t v = 0; v < meshlet.vertex_count; ++v) {
                    uint32_t localIdx = parent_meshlet_vertices[meshlet.vertex_offset + v];
                    parent.vertices.push_back(mergedVertices[localIdx]);
                    parent.origVertexIndices.push_back(mergedOrigVertexIndices[localIdx]);
                }
                
                parent.indices.reserve(meshlet.triangle_count * 3);
                for (uint32_t t = 0; t < meshlet.triangle_count; ++t) {
                    parent.indices.push_back(parent_meshlet_triangles[meshlet.triangle_offset + t * 3 + 0]);
                    parent.indices.push_back(parent_meshlet_triangles[meshlet.triangle_offset + t * 3 + 1]);
                    parent.indices.push_back(parent_meshlet_triangles[meshlet.triangle_offset + t * 3 + 2]);
                }
                
                // Compute bounds for the parent cluster
                meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                    &parent_meshlet_vertices[meshlet.vertex_offset],
                    &parent_meshlet_triangles[meshlet.triangle_offset],
                    meshlet.triangle_count,
                    &mergedVertices[0].pos.x,
                    mergedVertices.size(),
                    sizeof(MeshVertex)
                );
                parent.sphereCenterRadius = glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
                parent.coneAxisCutoff = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff);
                parent.lodSphereCenterRadius = groupUnionSphere;
                
                parent.selfError = selfError;
                parent.parentError = 1e30f; // Roots by default
                
                // Link children to this parent
                for (uint32_t childIdx : G) {
                    parent.childClusterIndices.push_back(childIdx);
                }
                
                nextLevelClusters.push_back(parent);
            }
        }
        
        // Add parent clusters to the global list
        allClusters.insert(allClusters.end(), nextLevelClusters.begin(), nextLevelClusters.end());
        
        std::cout << "[DAG Builder] Level " << level << " generated: " << nextLevelClusters.size() << " clusters." << std::endl;
        
        currentLevelStart = nextLevelStart;
        currentLevelCount = static_cast<uint32_t>(nextLevelClusters.size());
        level++;
    }
    
    std::cout << "[DAG Builder] Hierarchy completed. Total DAG clusters: " << allClusters.size() << std::endl;
    
    // ==========================================
    // 3. Pack All Clusters into MeshletData
    // ==========================================
    MeshletData packedData{};
    packedData.clusterCount = static_cast<uint32_t>(allClusters.size());
    
    packedData.flatVertices.reserve(allClusters.size() * 64);
    packedData.flatIndices.reserve(allClusters.size() * 126 * 3);
    packedData.indirectCommands.reserve(allClusters.size());
    packedData.boundsList.reserve(allClusters.size());
    
    for (size_t i = 0; i < allClusters.size(); ++i) {
        const auto& cluster = allClusters[i];
        
        uint32_t baseVertexOffset = static_cast<uint32_t>(packedData.flatVertices.size());
        uint32_t baseIndexOffset = static_cast<uint32_t>(packedData.flatIndices.size());
        
        // Copy vertices
        packedData.flatVertices.insert(packedData.flatVertices.end(), cluster.vertices.begin(), cluster.vertices.end());
        
        // Copy indices
        packedData.flatIndices.insert(packedData.flatIndices.end(), cluster.indices.begin(), cluster.indices.end());
        
        // Create indirect command
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = static_cast<uint32_t>(cluster.indices.size());
        cmd.instanceCount = 1;
        cmd.firstIndex = baseIndexOffset;
        cmd.vertexOffset = static_cast<int32_t>(baseVertexOffset);
        cmd.firstInstance = static_cast<uint32_t>(i); // Stores index in compute input
        
        packedData.indirectCommands.push_back(cmd);
        
        // Construct bounds and LOD parameters
        MeshletBounds b{};
        b.sphereCenterRadius = cluster.sphereCenterRadius;
        b.coneAxisCutoff = cluster.coneAxisCutoff;
        b.lodSphereCenterRadius = cluster.lodSphereCenterRadius;
        b.parentLodSphereCenterRadius = cluster.parentLodSphereCenterRadius;
        b.lodParams = glm::vec4(cluster.selfError, cluster.parentError, 0.0f, 0.0f);
        
        packedData.boundsList.push_back(b);
    }
    
    // Save original geometry for traditional rendering
    packedData.originalVertices = vertices;
    packedData.originalIndices = indices;
    
    return packedData;
}
