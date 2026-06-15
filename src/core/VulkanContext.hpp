#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <optional>

class VulkanContext {
public:
    VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    // Prevent copying
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    GLFWwindow* getWindow() const { return window; }
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    VkSurfaceKHR getSurface() const { return surface; }

    VkSwapchainKHR getSwapChain() const { return swapChain; }
    const std::vector<VkImage>& getSwapChainImages() const { return swapChainImages; }
    VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
    const std::vector<VkImageView>& getSwapChainImageViews() const { return swapChainImageViews; }

    VkCommandPool getCommandPool() const { return commandPool; }
    
    // Double buffering accessors
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t getCurrentFrameIndex() const { return currentFrame; }
    VkCommandBuffer getCurrentCommandBuffer() const { return commandBuffers[currentFrame]; }
    VkSemaphore getCurrentImageAvailableSemaphore() const { return imageAvailableSemaphores[currentFrame]; }
    VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const { return renderFinishedSemaphores[imageIndex]; }
    VkFence getCurrentInFlightFence() const { return inFlightFences[currentFrame]; }

    void advanceFrame() {
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // Helper to read binary files (like SPIR-V shader files)
    static std::vector<char> readFile(const std::string& filename);
    
    // Helper to create shader modules
    VkShaderModule createShaderModule(const std::vector<char>& code);

    // Swapchain details
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

private:
    void initVulkan();
    void cleanup();

    void createInstance();
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    
    void createSwapChain();
    void createImageViews();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    // Window ref (owned by Engine)
    GLFWwindow* window;

    // Vulkan core handles
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // Swapchain handles
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    // Command Pool
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Double buffering sync and command lists
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    uint32_t currentFrame = 0;
};
