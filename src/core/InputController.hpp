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
    bool isMPressedThisFrame() const { return mPressedThisFrame; }
    bool isHPressedThisFrame() const { return hPressedThisFrame; }
    bool isVPressedThisFrame() const { return vPressedThisFrame; }
    bool isUpPressedThisFrame() const { return upPressedThisFrame; }
    bool isDownPressedThisFrame() const { return downPressedThisFrame; }
    bool isBPressedThisFrame() const { return bPressedThisFrame; }
    bool isLPressedThisFrame() const { return lPressedThisFrame; }
    bool is1PressedThisFrame() const { return key1PressedThisFrame; }
    bool is2PressedThisFrame() const { return key2PressedThisFrame; }

private:
    GLFWwindow* window = nullptr;
    CameraNode* cameraNode = nullptr;

    bool tabWasPressed = false;
    bool tabPressedThisFrame = false;

    bool fWasPressed = false;
    bool fPressedThisFrame = false;

    bool mWasPressed = false;
    bool mPressedThisFrame = false;

    bool hWasPressed = false;
    bool hPressedThisFrame = false;

    bool vWasPressed = false;
    bool vPressedThisFrame = false;

    bool upWasPressed = false;
    bool upPressedThisFrame = false;

    bool downWasPressed = false;
    bool downPressedThisFrame = false;

    bool bWasPressed = false;
    bool bPressedThisFrame = false;

    bool lWasPressed = false;
    bool lPressedThisFrame = false;

    bool key1WasPressed = false;
    bool key1PressedThisFrame = false;

    bool key2WasPressed = false;
    bool key2PressedThisFrame = false;
};
