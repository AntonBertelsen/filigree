#include "Application.hpp"

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
}

void Application::mainLoop() {
    // Keep running until the user closes the window
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

void Application::cleanup() {
    // Clean up Vulkan resources (in reverse order of creation)
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
