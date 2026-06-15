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
    VkQueue presentQueue = VK_NULL_HANDLE;            // Implicitly destroyed with device
    VkSurfaceKHR surface = VK_NULL_HANDLE;            // Explicitly destroyed

    // Swapchain handles
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;        // Explicitly destroyed
    std::vector<VkImage> swapChainImages;             // Implicitly destroyed with swapChain
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;     // Explicitly destroyed
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;  // Explicitly destroyed
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;      // Explicitly destroyed

    // Command allocation handles
    VkCommandPool commandPool = VK_NULL_HANDLE;        // Explicitly destroyed
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;    // Implicitly destroyed with commandPool

    // CPU-GPU Synchronization handles
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE; // Explicitly destroyed
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE; // Explicitly destroyed
    VkFence inFlightFence = VK_NULL_HANDLE;            // Explicitly destroyed

    // Struct to store indices of the Queue Families we need
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    // Struct to hold swapchain capabilities queried from a GPU
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
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

    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    // Swapchain configuration selectors
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    void createSwapChain();
    void createImageViews();

    void createGraphicsPipeline();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    void createCommandPool();
    void createCommandBuffer();
    void createSyncObjects();
    void drawFrame();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
};
