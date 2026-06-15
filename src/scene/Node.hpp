#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>

class Node {
public:
    Node();
    virtual ~Node();

    // Prevent copying
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Hierarchy management
    void addChild(std::unique_ptr<Node> child);
    Node* getParent() const { return parent; }
    const std::vector<std::unique_ptr<Node>>& getChildren() const { return children; }

    // Transform properties
    void setPosition(const glm::vec3& pos) { position = pos; dirty = true; }
    glm::vec3 getPosition() const { return position; }

    void setRotation(const glm::quat& rot) { rotation = rot; dirty = true; }
    glm::quat getRotation() const { return rotation; }

    void setScale(const glm::vec3& scl) { scale = scl; dirty = true; }
    glm::vec3 getScale() const { return scale; }

    // Update functions
    virtual void update(float deltaTime);
    void updateWorldMatrix(const glm::mat4& parentWorldMatrix);

    glm::mat4 getLocalMatrix();
    glm::mat4 getWorldMatrix() const { return worldMatrix; }

protected:
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 localMatrix{1.0f};
    glm::mat4 worldMatrix{1.0f};

    bool dirty = true;

    // Parent pointer is a raw non-owning reference. 
    // Since children are owned by the parent's std::unique_ptr, 
    // a raw pointer acts as a safe, non-owning "weak" reference that prevents circular ownership.
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
};
