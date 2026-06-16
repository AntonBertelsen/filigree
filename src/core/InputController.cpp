#include "InputController.hpp"

InputController::InputController(GLFWwindow* window, CameraNode* cameraNode)
    : window(window), cameraNode(cameraNode) {}

void InputController::update(float deltaTime) {
    // 1. Update camera movement and mouse controls
    if (cameraNode && window) {
        cameraNode->update(deltaTime, window);
    }

    // 2. Poll Tab Key (Model cycling)
    tabPressedThisFrame = false;
    if (window) {
        bool currentTabState = (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS);
        if (currentTabState && !tabWasPressed) {
            tabPressedThisFrame = true;
        }
        tabWasPressed = currentTabState;
    }

    // 3. Poll F Key (Freeze culling frustum)
    fPressedThisFrame = false;
    if (window) {
        bool currentFState = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
        if (currentFState && !fWasPressed) {
            fPressedThisFrame = true;
        }
        fWasPressed = currentFState;
    }
}
