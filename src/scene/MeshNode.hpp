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
    glm::vec3 color;

    bool operator==(const MeshVertex& other) const {
        return pos == other.pos && normal == other.normal && texCoord == other.texCoord && color == other.color;
    }
};

class MeshNode : public Node {
public:
    MeshNode(const std::string& modelPath);
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
