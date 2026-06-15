#include "Application.hpp"
#include <set>
#include <algorithm>

// We will enable validation layers only in Debug builds to avoid performance overhead in Release builds.
#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// The standard validation layer provided by the LunarG SDK
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// Device extensions we require for our application to run.
// VK_KHR_SWAPCHAIN_EXTENSION_NAME is the extension that allows presenting rendered frames to a window.
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

void Application::run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void Application::initWindow() {
    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // GLFW was originally designed for OpenGL, so it creates an OpenGL context by default.
    // We must tell it NOT to create an OpenGL context since we are using Vulkan.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Disable window resizing for simplicity in this initial phase
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create the actual window
    window = glfwCreateWindow(WIDTH, HEIGHT, "Filigree Vulkan", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
}

void Application::initVulkan() {
    createInstance();
    createSurface(); // Create surface right after instance, before GPU selection
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
}

void Application::mainLoop() {
    // Keep running until the user closes the window
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

void Application::cleanup() {
    // Clean up swapchain image views
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    // Clean up swapchain
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
    }

    // Clean up logical device
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }

    // Clean up surface
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    // Clean up instance
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    // Clean up windowing resources
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Application::createInstance() {
    // Check validation layers if enabled
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    // 1. VkApplicationInfo
    // This struct provides info about our application to the driver (can be used for driver-level optimizations).
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Filigree Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; // Target Vulkan 1.3

    // 2. VkInstanceCreateInfo
    // This struct tells the Vulkan driver which global extensions and validation layers we want to use.
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Get extensions required by GLFW to interface with the window system
    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Enable validation layers if requested
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // 3. Create the instance
    // In Vulkan, functions return a VkResult code instead of throwing exceptions.
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance! Error code: " + std::to_string(result));
    }
    
    std::cout << "Successfully created Vulkan instance!" << std::endl;
}

std::vector<const char*> Application::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    return extensions;
}

bool Application::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void Application::createSurface() {
    // GLFW abstracts the platform-specific window handle and creates the Vulkan surface for us
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

void Application::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& dev : devices) {
        if (isDeviceSuitable(dev)) {
            physicalDevice = dev;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    // Print out the chosen GPU name for verification
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "Selected GPU: " << deviceProperties.deviceName << std::endl;
}

void Application::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    // We use a std::set to ensure we only create one queue description per unique family index,
    // in case the graphics and presentation queues share the same queue family (very common).
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // We do not require any specialized physical device features yet
    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Enable device extensions (specifically the Swapchain extension)
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Modern Vulkan ignores device-specific validation layers (they are now deprecated).
    // The specification requires enabledLayerCount to be set to 0.
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device! Error code: " + std::to_string(result));
    }

    // Retrieve the graphics and presentation queue handles
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);

    std::cout << "Successfully created Vulkan logical device!" << std::endl;
}

bool Application::isDeviceSuitable(VkPhysicalDevice dev) {
    QueueFamilyIndices indices = findQueueFamilies(dev);

    // Verify the GPU supports the KHR Swapchain extension
    bool extensionsSupported = checkDeviceExtensionSupport(dev);

    // Verify the swapchain properties are adequate for our surface
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(dev);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool Application::checkDeviceExtensionSupport(VkPhysicalDevice dev) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

Application::QueueFamilyIndices Application::findQueueFamilies(VkPhysicalDevice dev) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        // Find a queue family that supports graphics commands
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        // Find a queue family that supports presenting images to our window surface
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

Application::SwapChainSupportDetails Application::querySwapChainSupport(VkPhysicalDevice dev) {
    SwapChainSupportDetails details;

    // 1. Query general capabilities (like resolution ranges, minimum image counts, transforms)
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

    // 2. Query supported pixel formats (color channels and color spaces)
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, details.formats.data());
    }

    // 3. Query supported presentation modes (V-Sync, Triple buffering, etc.)
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Application::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    // Look for sRGB color space with standard 8-bit RGBA channels.
    // sRGB gives us perceptually accurate colors.
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    // If our preferred format isn't found, just settle for the first available one
    return availableFormats[0];
}

VkPresentModeKHR Application::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Triple buffering (Mailbox) is the gold standard: no tearing, lowest input lag.
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            std::cout << "Presentation Mode: Triple Buffering (Mailbox)" << std::endl;
            return availablePresentMode;
        }
    }

    // FIFO is guaranteed by the spec to always be available, and it corresponds to standard V-Sync.
    std::cout << "Presentation Mode: Double Buffering V-Sync (FIFO)" << std::endl;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Application::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    // If the extent width is not uint32_t max, it means the operating system forces the swapchain
    // to match the exact size of the window coordinates.
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    } else {
        // High-DPI screens (like Apple Retina) might use different values for window size (screen coords)
        // and frame buffer size (pixels). We query the actual pixel resolution from GLFW.
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        // Clamp the values to the minimum and maximum boundaries supported by the hardware
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

void Application::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    // It's recommended to request 1 more image than the minimum required by the hardware.
    // This ensures we don't have to wait on the driver to finish rendering before we can acquire the next image.
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

    // Make sure we don't exceed the maximum supported swapchain images (0 means no limit)
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1; // 1 unless we are rendering 3D stereoscopic VR apps
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // Render target usage

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    // If the graphics and presentation queues are in different families, we must share the swapchain
    // images between them using CONCURRENT mode. If they are the same, EXCLUSIVE mode is faster.
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    // Set pre-transform (like rotating the screen for mobile apps). We want the identity transform.
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    // Set composite alpha (how to blend with other windows on the desktop). We ignore it.
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE; // Enable clipping (don't render pixels that are obscured by other windows)

    // Used if we recreate the swapchain (like on resize). We will link the old swapchain here later.
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // Retrieve the actual handles to the images created inside the swapchain
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

    std::cout << "Successfully created Vulkan Swapchain (" 
              << swapChainExtent.width << "x" << swapChainExtent.height 
              << ", " << imageCount << " images)!" << std::endl;
}

void Application::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;

        // Components allow you to swizzle color channels (we use identity mapping)
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // SubresourceRange defines what the image purpose is (color target, 1 mip level, 1 layer)
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image views!");
        }
    }

    std::cout << "Successfully created Vulkan Swapchain Image Views!" << std::endl;
}
