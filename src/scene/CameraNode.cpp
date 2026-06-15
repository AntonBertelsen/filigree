#include "CameraNode.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

CameraNode::CameraNode() {
    position = glm::vec3(0.0f, 0.0f, 3.0f); // default camera position
    updateCameraVectors();
}

glm::mat4 CameraNode::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 CameraNode::getProjectionMatrix(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    // Invert the Y axis for Vulkan projection coordinates
    proj[1][1] *= -1.0f;
    return proj;
}

void CameraNode::update(float deltaTime, GLFWwindow* window) {
    // 1. Mouse Input for Look (Only look around when holding right mouse button)
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = static_cast<float>(xpos - lastX);
        float yoffset = static_cast<float>(lastY - ypos); // reversed since y-coordinates go from bottom to top

        lastX = xpos;
        lastY = ypos;

        xoffset *= mouseSensitivity;
        yoffset *= mouseSensitivity;

        yaw   += xoffset;
        pitch += yoffset;

        // Make sure that when pitch is out of bounds, screen doesn't get flipped
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateCameraVectors();
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        firstMouse = true;
    }

    // 2. Keyboard Input for Fly WASD
    float velocity = movementSpeed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        position += front * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        position -= front * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        position -= right * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        position += right * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        position += worldUp * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        position -= worldUp * velocity;
    }

    // Call base class update to update hierarchy child nodes
    Node::update(deltaTime);
}

void CameraNode::updateCameraVectors() {
    glm::vec3 dir;
    dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    dir.y = sin(glm::radians(pitch));
    dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    
    front = glm::normalize(dir);
    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::normalize(glm::cross(right, front));

    // Update the Node's rotation quaternion as well to match
    rotation = glm::quat_cast(glm::inverse(glm::lookAt(glm::vec3(0.0f), front, up)));
}
