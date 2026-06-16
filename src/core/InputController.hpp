#pragma once

#include <GLFW/glfw3.h>
#include "scene/CameraNode.hpp"

class InputController {
public:
    InputController(GLFWwindow* window, CameraNode* cameraNode);
    ~InputController() = default;

    void update(float deltaTime);

    bool isTabPressedThisFrame() const { return tabPressedThisFrame; }
    bool isFPressedThisFrame() const { return fPressedThisFrame; }

private:
    GLFWwindow* window = nullptr;
    CameraNode* cameraNode = nullptr;

    bool tabWasPressed = false;
    bool tabPressedThisFrame = false;

    bool fWasPressed = false;
    bool fPressedThisFrame = false;
};
