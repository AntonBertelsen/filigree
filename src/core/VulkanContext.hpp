#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

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
    
    VkImageView getDepthImageView() const { return depthImageView; }
    VkFormat getDepthFormat() const { return depthFormat; }
    VkImage getDepthImage() const { return depthImage; }

    VkImage getHzbImage(uint32_t frameIndex) const { return hzbImages[frameIndex]; }
    VkImageView getHzbImageView(uint32_t frameIndex) const { return hzbImageViews[frameIndex]; }
    VkImageView getHzbLevelImageView(uint32_t frameIndex, uint32_t level) const { return hzbLevelImageViews[frameIndex][level]; }
    static constexpr uint32_t HZB_MIP_LEVELS = 11;
    static constexpr uint32_t HZB_WIDTH = 1024;
    static constexpr uint32_t HZB_HEIGHT = 1024;

    VkCommandPool getCommandPool() const { return commandPool; }
    VmaAllocator getAllocator() const { return allocator; }

    // Swapchain recreation support
    void recreateSwapChain();
    void cleanupSwapChain();

    // Buffer helpers (using VMA)
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkBuffer& buffer, VmaAllocation& allocation);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    
    // Command submission helpers for transfer operations
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cb);
    
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
    void createDepthResources();
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

    // Depth buffer handles
    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    // HZB handles (double buffered)
    VkImage hzbImages[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VmaAllocation hzbImageAllocations[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView hzbImageViews[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView hzbLevelImageViews[2][11] = {};

    void createHzbResources();
    void cleanupHzbResources();

    // Command Pool
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Double buffering sync and command lists
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    uint32_t currentFrame = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
};
