#pragma once

#include "Node.hpp"

#include <GLFW/glfw3.h>

class CameraNode : public Node {
public:
    CameraNode();
    virtual ~CameraNode() = default;

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspect) const;

    void update(float deltaTime, GLFWwindow* window);

    float getFov() const { return fov; }
    void setFov(float f) { fov = f; }

private:
    void updateCameraVectors();

    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    float yaw = -90.0f;
    float pitch = 0.0f;

    float movementSpeed = 2.5f;
    float mouseSensitivity = 0.1f;

    double lastX = 400.0;
    double lastY = 300.0;
    bool firstMouse = true;
};
