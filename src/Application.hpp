#pragma once

// Tell GLFW to include the Vulkan headers automatically
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

class Application {
public:
    void run();

private:
    // Window configuration
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    GLFWwindow* window = nullptr;

    // Vulkan core handles
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; // Implicitly destroyed with instance
    VkDevice device = VK_NULL_HANDLE;                 // Explicitly destroyed
    VkQueue graphicsQueue = VK_NULL_HANDLE;           // Implicitly destroyed with device

    // Struct to store indices of the Queue Families we need
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;

        bool isComplete() const {
            return graphicsFamily.has_value();
        }
    };

    // Core steps of the application lifecycle
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();

    // Helper functions
    void createInstance();
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    void pickPhysicalDevice();
    void createLogicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
};
