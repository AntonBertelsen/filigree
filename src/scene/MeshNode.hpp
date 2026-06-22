#pragma once

#include "Node.hpp"
#include <vector>
#include <string>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    bool operator==(const MeshVertex& other) const {
        return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
    }
};

class MeshNode : public Node {
public:
    MeshNode(const std::string& modelPath);
    MeshNode(const std::string& modelPath, const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);
    virtual ~MeshNode() = default;

    const std::vector<MeshVertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }
    const std::string& getModelPath() const { return modelPath; }

private:
    void loadModel();

    std::string modelPath;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};
