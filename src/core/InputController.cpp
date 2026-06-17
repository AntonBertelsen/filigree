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

    // 4. Poll M Key (Cycle models)
    mPressedThisFrame = false;
    if (window) {
        bool currentMState = (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS);
        if (currentMState && !mWasPressed) {
            mPressedThisFrame = true;
        }
        mWasPressed = currentMState;
    }

    // 5. Poll H Key (Toggle HZB culling)
    hPressedThisFrame = false;
    if (window) {
        bool currentHState = (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS);
        if (currentHState && !hWasPressed) {
            hPressedThisFrame = true;
        }
        hWasPressed = currentHState;
    }

    // 6. Poll V Key (Toggle HZB visualizer)
    vPressedThisFrame = false;
    if (window) {
        bool currentVState = (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS);
        if (currentVState && !vWasPressed) {
            vPressedThisFrame = true;
        }
        vWasPressed = currentVState;
    }

    // 7. Poll Up Key (Increment HZB mip level)
    upPressedThisFrame = false;
    if (window) {
        bool currentUpState = (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
        if (currentUpState && !upWasPressed) {
            upPressedThisFrame = true;
        }
        upWasPressed = currentUpState;
    }

    // 8. Poll Down Key (Decrement HZB mip level)
    downPressedThisFrame = false;
    if (window) {
        bool currentDownState = (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
        if (currentDownState && !downWasPressed) {
            downPressedThisFrame = true;
        }
        downWasPressed = currentDownState;
    }

    // 9. Poll B Key (Toggle bounding spheres visualizer)
    bPressedThisFrame = false;
    if (window) {
        bool currentBState = (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS);
        if (currentBState && !bWasPressed) {
            bPressedThisFrame = true;
        }
        bWasPressed = currentBState;
    }

    // 10. Poll L Key (Toggle LOD)
    lPressedThisFrame = false;
    if (window) {
        bool currentLState = (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS);
        if (currentLState && !lWasPressed) {
            lPressedThisFrame = true;
        }
        lWasPressed = currentLState;
    }

    // 11. Poll 1 Key (Decrease LOD Threshold)
    key1PressedThisFrame = false;
    if (window) {
        bool current1State = (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS);
        if (current1State && !key1WasPressed) {
            key1PressedThisFrame = true;
        }
        key1WasPressed = current1State;
    }

    // 12. Poll 2 Key (Increase LOD Threshold)
    key2PressedThisFrame = false;
    if (window) {
        bool current2State = (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS);
        if (current2State && !key2WasPressed) {
            key2PressedThisFrame = true;
        }
        key2WasPressed = current2State;
    }
}
