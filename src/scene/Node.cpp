#include "Node.hpp"

#include <glm/gtc/matrix_transform.hpp>

Node::Node() : parent(nullptr), dirty(true), localMatrix(1.0f), worldMatrix(1.0f) {}

Node::~Node() {
    // std::unique_ptr automatically handles clean destruction of children
}

void Node::addChild(std::unique_ptr<Node> child) {
    if (child) {
        child->parent = this;
        child->dirty = true;
        children.push_back(std::move(child));
    }
}

void Node::update(float deltaTime) {
    // Base implementation updates all children
    for (auto& child : children) {
        child->update(deltaTime);
    }
}

void Node::updateWorldMatrix(const glm::mat4& parentWorldMatrix) {
    worldMatrix = parentWorldMatrix * getLocalMatrix();
    for (auto& child : children) {
        child->updateWorldMatrix(worldMatrix);
    }
}

glm::mat4 Node::getLocalMatrix() {
    if (dirty) {
        glm::mat4 translationMat = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotationMat = glm::mat4_cast(rotation);
        glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);
        
        localMatrix = translationMat * rotationMat * scaleMat;
        dirty = false;
    }
    return localMatrix;
}
