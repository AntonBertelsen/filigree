#include "InputController.hpp"

InputController::InputController(GLFWwindow* window, CameraNode* cameraNode)
    : window(window), cameraNode(cameraNode) {}

void InputController::update(float deltaTime, bool suspendCamera, bool wantCaptureKeyboard) {
    // 1. Update camera movement and mouse controls (only if camera controls are not suspended)
    if (cameraNode && window && !suspendCamera) {
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

    // 13. Poll 3 Key (Cycle VisBuffer Debug Mode)
    key3PressedThisFrame = false;
    if (window) {
        bool current3State = (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS);
        if (current3State && !key3WasPressed) {
            key3PressedThisFrame = true;
        }
        key3WasPressed = current3State;
    }

    // 14. Poll 4 Key (Toggle Shading Path)
    key4PressedThisFrame = false;
    if (window) {
        bool current4State = (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS);
        if (current4State && !key4WasPressed) {
            key4PressedThisFrame = true;
        }
        key4WasPressed = current4State;
    }

    // 15. Poll 5 Key (Toggle Rasterizer Mode)
    key5PressedThisFrame = false;
    if (window) {
        bool current5State = (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS);
        if (current5State && !key5WasPressed) {
            key5PressedThisFrame = true;
        }
        key5WasPressed = current5State;
    }

    // 16. Poll 6 Key (Toggle Hardware Path Mode)
    key6PressedThisFrame = false;
    if (window) {
        bool current6State = (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS);
        if (current6State && !key6WasPressed) {
            key6PressedThisFrame = true;
        }
        key6WasPressed = current6State;
    }

    // 17. Poll 7 Key (Toggle Synchronization Mode)
    key7PressedThisFrame = false;
    if (window) {
        bool current7State = (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS);
        if (current7State && !key7WasPressed) {
            key7PressedThisFrame = true;
        }
        key7WasPressed = current7State;
    }

    // Clear all hotkey inputs if keyboard focus is captured
    if (wantCaptureKeyboard) {
        tabPressedThisFrame = false;
        fPressedThisFrame = false;
        mPressedThisFrame = false;
        hPressedThisFrame = false;
        vPressedThisFrame = false;
        upPressedThisFrame = false;
        downPressedThisFrame = false;
        bPressedThisFrame = false;
        lPressedThisFrame = false;
        key1PressedThisFrame = false;
        key2PressedThisFrame = false;
        key3PressedThisFrame = false;
        key4PressedThisFrame = false;
        key5PressedThisFrame = false;
        key6PressedThisFrame = false;
        key7PressedThisFrame = false;
    }
}
