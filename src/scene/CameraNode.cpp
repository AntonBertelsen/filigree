#include "CameraNode.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

static float accumulatedScrollY = 0.0f;
static void cameraScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    accumulatedScrollY += static_cast<float>(yoffset);
}

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
    // Register scroll callback on the first frame
    static bool callbackSet = false;
    if (!callbackSet) {
        glfwSetScrollCallback(window, cameraScrollCallback);
        callbackSet = true;
    }

    bool leftMousePressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    bool rightMousePressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    bool shiftPressed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

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

    bool isFlyMode = leftMousePressed && shiftPressed;
    bool isOrbitMode = leftMousePressed && !shiftPressed;
    bool isPanMode = rightMousePressed;

    if (isOrbitMode) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        
        // Orbit Mode: Rotate yaw and pitch around target
        yaw += xoffset;
        pitch += yoffset;

        // Constrain pitch
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateCameraVectors();
        position = target - front * radius;
    } else if (isFlyMode) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Fly Mode: Look around from current position
        yaw += xoffset;
        pitch += yoffset;

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateCameraVectors();

        // Keyboard Movement (WASD + Space/Ctrl + Q/E) in Fly Mode
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
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            position += worldUp * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            position -= worldUp * velocity;
        }

        // Recalculate target position based on movement
        target = position + front * radius;
    } else if (isPanMode) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Pan Mode: Translate both target and position in camera plane
        float panSpeed = 0.005f;
        glm::vec3 panX = -right * (xoffset * panSpeed * radius);
        glm::vec3 panY = -up * (yoffset * panSpeed * radius);
        glm::vec3 panOffset = panX + panY;
        position += panOffset;
        target += panOffset;
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        firstMouse = true;
    }

    // Process scroll wheel zoom internally
    if (accumulatedScrollY != 0.0f) {
        if (isFlyMode) {
            // Move camera position forward/backward in Fly Mode
            float zoomSpeed = 0.5f;
            position += front * (accumulatedScrollY * zoomSpeed);
            target = position + front * radius;
        } else {
            // Orbit Zoom: decrease/increase radius
            float zoomFactor = 0.1f;
            // Exponential zoom scaling keeps transition smooth at different distances
            radius -= accumulatedScrollY * zoomFactor * radius;
            if (radius < 0.2f) radius = 0.2f;
            if (radius > 50.0f) radius = 50.0f;
            
            position = target - front * radius;
        }
        accumulatedScrollY = 0.0f; // reset
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


