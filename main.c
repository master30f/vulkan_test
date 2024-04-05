#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "glfw3.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include "cglm/cglm.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#define QFI_GRAPHICS_BIT 0b01
#define QFI_PRESENT_BIT  0b10
#define QFI_COMPLETE     (QFI_GRAPHICS_BIT | QFI_PRESENT_BIT)

#define ARR_LEN(array) (sizeof((array))/sizeof((array)[0]))

#define FATAL(...) do \
{ \
    fprintf(stderr, "\e[1;31mFATAL:\e[0m " __VA_ARGS__); \
    exit(-1); \
} while (0)

#define ERROR(...) \
    fprintf(stderr, "\e[0;31mERROR:\e[0m " __VA_ARGS__)

#define WARN(...) \
    fprintf(stderr, "\e[0;33mWARN:\e[0m " __VA_ARGS__)

#define INFO(...) \
    printf("\e[0;36mINFO:\e[0m " __VA_ARGS__)

#define LOG(...) \
    printf(__VA_ARGS__)

#define VK_TRY(expr, fail) do \
{ \
    VkResult result = expr; \
    if (result != VK_SUCCESS) fail; \
} while (0)

#define bufalloc(buffer, newCount) do \
{ \
    if ((buffer)->count < (newCount)) \
    { \
        if ((buffer)->data != NULL) free((buffer)->data); \
        (buffer)->count = (newCount); \
        (buffer)->data  = malloc((newCount)); \
    } \
} while (0)

typedef struct {
    uint32_t count;
    char *data;
} ByteBuf;


int clamp(int x, int min, int max)
{
    int y = x < min ? min : x;
    return y > max ? max : y;
}

// FIXME: make this not sketchy and handle errors
ByteBuf readFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    fseek(file, 0, SEEK_END);
    uint32_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    ByteBuf buffer = { 0 };
    bufalloc(&buffer, size);
    fread(buffer.data, size, 1, file);
    fclose(file);

    return buffer;
}

typedef struct {
    vec3 pos;
    vec3 color;
} Vertex;

typedef struct {
    CGLM_ALIGN_MAT mat4 model;
    CGLM_ALIGN_MAT mat4 view;
    CGLM_ALIGN_MAT mat4 proj;
} UniformBufferObject;


#define TITLE            "Vulkan test"
#define WINDOW_WIDTH     800
#define WINDOW_HEIGHT    600

#define FRAMES_IN_FLIGHT 2


// TODO: support more validation layers
const char              *validationLayer       = "VK_LAYER_KHRONOS_validation";

const char              *deviceExtensions[]    = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
const uint32_t           deviceExtensionsCount = ARR_LEN(deviceExtensions);

const Vertex vertices[] = {
    {{ -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }},
    {{  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }},
    {{  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }},
    {{ -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 0.0f }}
};

const uint16_t indices[] = {
    0, 1, 2, 2, 3, 0
};


GLFWwindow              *window;

VkInstance               instance;

VkSurfaceKHR             surface;

VkPhysicalDevice         physicalDevice        = VK_NULL_HANDLE;
uint32_t                 graphicsFamilyIndex   = 0;
uint32_t                 presentFamilyIndex    = 0;
VkSurfaceCapabilitiesKHR swapCapabilities;
VkPresentModeKHR        *swapPresentModes      = NULL;
VkSurfaceFormatKHR      *swapFormats           = NULL;

VkDevice                 device;
VkQueue                  graphicsQueue;
VkQueue                  presentQueue;

VkSwapchainKHR           swapchain;
VkFormat                 swapchainImageFormat;
VkExtent2D               swapchainExtent;
VkImage                 *swapchainImages       = NULL;

VkImageView             *swapchainImageViews   = NULL;

VkRenderPass             renderPass;

VkDescriptorSetLayout    descriptorSetLayout;

VkPipelineLayout         pipelineLayout;
VkPipeline               graphicsPipeline;

VkFramebuffer           *swapchainFramebuffers = NULL;

VkCommandPool            commandPool;

VkCommandBuffer          commandBuffers[FRAMES_IN_FLIGHT];

VkImage                  textureImage;
VkDeviceMemory           textureImageMemory;

VkBuffer                 vertexBuffer;
VkDeviceMemory           vertexBufferMemory;

VkBuffer                 indexBuffer;
VkDeviceMemory           indexBufferMemory;

// TODO: merge
VkBuffer                 uniformBuffers[FRAMES_IN_FLIGHT];
VkDeviceMemory           uniformBufferMemories[FRAMES_IN_FLIGHT];
void                    *mappedUniformBuffers[FRAMES_IN_FLIGHT];

VkDescriptorPool         descriptorPool;

VkDescriptorSet          descriptorSets[FRAMES_IN_FLIGHT];

VkSemaphore              imageAvailableSemaphores[FRAMES_IN_FLIGHT];
VkSemaphore              renderFinishedSemaphores[FRAMES_IN_FLIGHT];
VkFence                  inFlightFences[FRAMES_IN_FLIGHT];

uint8_t                  currentFrame          = 0;
bool                     framebufferResized    = false;

// TODO: investtigate more accurate / better FPS measuring methods (prolly no longer necessary though)
struct timespec          lastFrameEnd;
double                   deltaTime             = 0;


// TODO: keep drawing the window while resizing?
static void framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    (void) window;
    (void) width;
    (void) height;

    framebufferResized = true;
}

static inline void createWindow(void)
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, TITLE, NULL, NULL);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}


static inline void enableValidationLayers(VkInstanceCreateInfo *createInfo)
{
    // TODO: change all of these dynamic arrays to buffers
    uint32_t layersCount      = 0;
    VkLayerProperties *layers = NULL;
    VK_TRY(vkEnumerateInstanceLayerProperties(&layersCount, NULL), {
        ERROR("could not get available validation layers: %s\n", string_VkResult(result));
        return;
    });
    arrsetlen(layers, layersCount);
    vkEnumerateInstanceLayerProperties(&layersCount, layers);

    bool found = false;
    for (uint32_t i = 0; i < layersCount; i++)
    {
        if (strcmp(layers[i].layerName, validationLayer) == 0)
        {
            found = true;
            break;
        }
    }

    arrfree(layers);

    if (found)
    {
        createInfo->ppEnabledLayerNames = &validationLayer;
        createInfo->enabledLayerCount   = 1;

        INFO("enabled validation layers:\n");
        LOG("    - %s\n", validationLayer);
    }
    else ERROR("could not enable validation layers: layer does not exist\n");
}

static inline void createVulkanInstance(void)
{
    uint32_t glfwExtensionsCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

    VkApplicationInfo appInfo          = { 0 };
    appInfo.sType                      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName           = TITLE;
    appInfo.applicationVersion         = VK_MAKE_VERSION(0, 0, 0);
    appInfo.pEngineName                = "No engine";
    appInfo.engineVersion              = VK_MAKE_VERSION(0, 0, 0);
    appInfo.apiVersion                 = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo    = { 0 };
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = glfwExtensionsCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

#ifdef DEBUG
    enableValidationLayers(&createInfo);
#endif // DEBUG

    VK_TRY(vkCreateInstance(&createInfo, NULL, &instance), FATAL("could not create vulkan instance: %s\n", string_VkResult(result)));
}


static inline void createWindowSurface(void)
{
    VK_TRY(glfwCreateWindowSurface(instance, window, NULL, &surface), FATAL("cound not create window surface: %s\n", string_VkResult(result)));
}


static inline bool physicalDeviceSupportsSurfaceKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface)
{
    VkBool32 supports = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &supports);
    return supports;
}

static inline bool checkQueueFamilies(VkPhysicalDevice device, VkQueueFamilyProperties *queueFamilies)
{
    uint8_t  QFIBitmap = 0;
    uint32_t queueFamiliesCount;

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamiliesCount, NULL);
    arrsetlen(queueFamilies, queueFamiliesCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamiliesCount, queueFamilies);

    for (uint32_t i = 0; i < queueFamiliesCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphicsFamilyIndex = i;
            QFIBitmap |= QFI_GRAPHICS_BIT;
        }
        if (physicalDeviceSupportsSurfaceKHR(device, i, surface))
        {
            presentFamilyIndex = i;
            QFIBitmap |= QFI_PRESENT_BIT;
        }

        if (QFIBitmap == QFI_COMPLETE) return true;
    }

    return QFIBitmap == QFI_COMPLETE;
}

static inline bool checkExtensions(VkPhysicalDevice device, VkExtensionProperties *extensions)
{
    uint32_t extensionsCount;

    VK_TRY(vkEnumerateDeviceExtensionProperties(device, NULL, &extensionsCount, NULL), continue);
    arrsetlen(extensions, extensionsCount);
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionsCount, extensions);

    uint32_t foundExtensions = 0;
    for (uint32_t i = 0; i < extensionsCount; i++)
    {
        for (uint32_t j = 0; j < deviceExtensionsCount; j++)
        {
            if (strcmp(extensions[i].extensionName, deviceExtensions[j]) == 0)
            {
                foundExtensions++;
                break;
            }
        }
    }

    return foundExtensions == deviceExtensionsCount;
}

static inline bool checkSwapchainCapabilities(VkPhysicalDevice device)
{
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &swapCapabilities);

    uint32_t swapFormatsCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &swapFormatsCount, NULL);
    if (swapFormatsCount == 0) return false;
    arrsetlen(swapFormats, swapFormatsCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &swapFormatsCount, swapFormats);

    uint32_t swapPresentModesCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &swapPresentModesCount, NULL);
    if (swapPresentModesCount == 0) return false;
    arrsetlen(swapPresentModes, swapPresentModesCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &swapPresentModesCount, swapPresentModes);

    return true;
}

static inline void findSuitableGPU(void)
{
    uint32_t devicesCount                  = 0;
    VkPhysicalDevice *devices              = NULL;
    VK_TRY(vkEnumeratePhysicalDevices(instance, &devicesCount, NULL), FATAL("could not get physical devices: %s\n", string_VkResult(result)));
    arrsetlen(devices, devicesCount);
    vkEnumeratePhysicalDevices(instance, &devicesCount, devices);

    VkQueueFamilyProperties *queueFamilies = NULL;
    VkExtensionProperties *extensions      = NULL;

    for (uint32_t i = 0; i < devicesCount; i++)
    {
        VkPhysicalDevice device = devices[i];

        if (!checkQueueFamilies(device, queueFamilies)) continue;
        if (!checkExtensions(device, extensions))       continue;
        if (!checkSwapchainCapabilities(device))        continue;

        physicalDevice = device;
        break;
    }

    arrfree(extensions);
    arrfree(queueFamilies);
    arrfree(devices);

    if (physicalDevice == VK_NULL_HANDLE) FATAL("no suitable GPUs found!\n");

    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    INFO("selected GPU: %s\n", physicalDeviceProperties.deviceName);

    INFO("GFI: %d PFI: %d\n", graphicsFamilyIndex, presentFamilyIndex);
}


static inline void createLogicalDevice(void)
{
    float queuePriority                             = 1.0f;

    // TODO: this is ugly af
    VkDeviceQueueCreateInfo *queueCreateInfos       = NULL;
    arrsetcap(queueCreateInfos, 2);

    VkDeviceQueueCreateInfo graphicsQueueCreateInfo = { 0 };
    graphicsQueueCreateInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphicsQueueCreateInfo.queueFamilyIndex        = graphicsFamilyIndex;
    graphicsQueueCreateInfo.pQueuePriorities        = &queuePriority;
    graphicsQueueCreateInfo.queueCount              = 1;
    arrput(queueCreateInfos, graphicsQueueCreateInfo);

    if (graphicsFamilyIndex != presentFamilyIndex)
    {
        VkDeviceQueueCreateInfo presentQueueCreateInfo = { 0 };
        presentQueueCreateInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        presentQueueCreateInfo.queueFamilyIndex        = presentFamilyIndex;
        presentQueueCreateInfo.pQueuePriorities        = &queuePriority;
        presentQueueCreateInfo.queueCount              = 1;
        arrput(queueCreateInfos, presentQueueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures         = { 0 };

    VkDeviceCreateInfo createInfo                   = { 0 };
    createInfo.sType                                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos                    = queueCreateInfos;
    createInfo.queueCreateInfoCount                 = arrlen(queueCreateInfos);
    createInfo.pEnabledFeatures                     = &deviceFeatures;
    createInfo.ppEnabledExtensionNames              = deviceExtensions;
    createInfo.enabledExtensionCount                = deviceExtensionsCount;
#ifdef DEBUG
    createInfo.ppEnabledLayerNames                  = &validationLayer;
    createInfo.enabledLayerCount                    = 1;
#endif // DEBUG

    VK_TRY(vkCreateDevice(physicalDevice, &createInfo, NULL, &device), FATAL("could not instantiate logical device: %s\n", string_VkResult(result)));

    // TODO: confirm if this is cool or not?
    arrfree(queueCreateInfos);

    INFO("enabled device extensions:\n");
    for (uint32_t i = 0; i < deviceExtensionsCount; i++)
    {
        LOG("    - %s\n", deviceExtensions[i]);
    }

    vkGetDeviceQueue(device, graphicsFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamilyIndex, 0, &presentQueue);
}


static inline VkSurfaceFormatKHR selectSwapSurfaceFormat(const VkSurfaceFormatKHR *availableFormats)
{
    for (int i = 0; i < arrlen(availableFormats); i++) {
        VkSurfaceFormatKHR availableFormat = availableFormats[i];

        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }

    VkSurfaceFormatKHR subOptiomalFormat = availableFormats[0];
    WARN("selected suboptimal surface format: %s %s\n", string_VkFormat(subOptiomalFormat.format), string_VkColorSpaceKHR(subOptiomalFormat.colorSpace));
    return subOptiomalFormat;
}

static inline VkPresentModeKHR selectSwapPresentMode(const VkPresentModeKHR *availableModes)
{
    for (int i = 0; i < arrlen(availableModes); i++)
    {
        VkPresentModeKHR availableMode = availableModes[i];

        if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availableMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static inline VkExtent2D selectSwapExtent(const VkSurfaceCapabilitiesKHR *capabilities)
{
    if (capabilities->currentExtent.width != UINT32_MAX) return capabilities->currentExtent;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return (VkExtent2D){
        .width  = clamp(width, capabilities->minImageExtent.width, capabilities->maxImageExtent.width),
        .height = clamp(height, capabilities->minImageExtent.height, capabilities->maxImageExtent.height)
    };
}


static void createSwapchain(void)
{
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &swapCapabilities);

    uint32_t queueFamilyIndices[]       = { graphicsFamilyIndex, presentFamilyIndex };

    VkSurfaceFormatKHR surfaceFormat    = selectSwapSurfaceFormat(swapFormats);
    VkPresentModeKHR presentMode        = selectSwapPresentMode(swapPresentModes);
    swapchainExtent                     = selectSwapExtent(&swapCapabilities);
    swapchainImageFormat                = surfaceFormat.format;

    uint32_t imageCount                 = swapCapabilities.minImageCount + 1;
    if (swapCapabilities.maxImageCount != 0 && imageCount > swapCapabilities.maxImageCount)
    {
        imageCount = swapCapabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo  = { 0 };
    createInfo.sType                     = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface                   = surface;
    createInfo.minImageCount             = imageCount;
    createInfo.imageFormat               = surfaceFormat.format;
    createInfo.imageColorSpace           = surfaceFormat.colorSpace;
    createInfo.imageExtent               = swapchainExtent;
    createInfo.imageArrayLayers          = 1;
    createInfo.imageUsage                = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform              = swapCapabilities.currentTransform;
    createInfo.compositeAlpha            = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode               = presentMode;
    createInfo.clipped                   = VK_TRUE;
    createInfo.oldSwapchain              = VK_NULL_HANDLE;

    if (graphicsFamilyIndex != presentFamilyIndex)
    {
        createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices   = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_TRY(vkCreateSwapchainKHR(device, &createInfo, NULL, &swapchain), FATAL("could not create swapchain: %s\n", string_VkResult(result)));

    uint32_t swapchainImagesCount;
    VK_TRY(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImagesCount, NULL), FATAL("could not get swapchain images: %s\n", string_VkResult(result)));
    arrsetlen(swapchainImages, imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImagesCount, swapchainImages);
}


static void createImageViews(void)
{
    arrsetlen(swapchainImageViews, arrlen(swapchainImages));

    for (int i = 0; i < arrlen(swapchainImages); i++)
    {
        VkImageViewCreateInfo createInfo           = { 0 };
        createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image                           = swapchainImages[i];
        createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format                          = swapchainImageFormat;
        createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel   = 0;
        createInfo.subresourceRange.levelCount     = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount     = 1;

        VK_TRY(vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]), FATAL("could not create swap image view: %s\n", string_VkResult(result)));
    }

    INFO("created %lld swapchain image views\n", arrlen(swapchainImageViews));
}


static inline void createRenderPass(void)
{
    VkSubpassDependency dependency           = { 0 };
    dependency.srcSubpass                    = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask                  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask                 = 0;
    dependency.dstSubpass                    = 0;
    dependency.dstStageMask                  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask                 = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription colorAttachment  = { 0 };
    colorAttachment.format                   = swapchainImageFormat;
    colorAttachment.samples                  = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp                   = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp                  = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout              = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = { 0 };
    colorAttachmentRef.attachment            = 0;
    colorAttachmentRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass             = { 0 };
    subpass.pipelineBindPoint                = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount             = 1;
    subpass.pColorAttachments                = &colorAttachmentRef;

    VkRenderPassCreateInfo createInfo        = { 0 };
    createInfo.sType                         = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.dependencyCount               = 1;
    createInfo.pDependencies                 = &dependency;
    createInfo.attachmentCount               = 1;
    createInfo.pAttachments                  = &colorAttachment;
    createInfo.subpassCount                  = 1;
    createInfo.pSubpasses                    = &subpass;

    VK_TRY(vkCreateRenderPass(device, &createInfo, NULL, &renderPass), FATAL("could not create render pass: %s\n", string_VkResult(result)));
}


static inline void createDescriptorSetLayout(void)
{
    VkDescriptorSetLayoutBinding layoutBinding = { 0 };
    layoutBinding.binding                      = 0;
    layoutBinding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount              = 1;
    layoutBinding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo = { 0 };
    createInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount                    = 1;
    createInfo.pBindings                       = &layoutBinding;

    VK_TRY(vkCreateDescriptorSetLayout(device, &createInfo, NULL, &descriptorSetLayout), FATAL("could not create descriptor set layout: %s\n", string_VkResult(result)));
}


static VkShaderModule createShaderModule(ByteBuf code)
{
    VkShaderModuleCreateInfo createInfo = { 0 };
    createInfo.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize                 = code.count;
    // FIXME: investigate this?
    createInfo.pCode                    = (uint32_t *)code.data;

    VkShaderModule shaderModule;
    VK_TRY(vkCreateShaderModule(device, &createInfo, NULL, &shaderModule), FATAL("could not create shader module: %s\n", string_VkResult(result)));

    free(code.data);

    return shaderModule;
}

static inline void createGraphicsPipeline(void)
{
    {
        VkPipelineLayoutCreateInfo createInfo = { 0 };
        createInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount             = 1;
        createInfo.pSetLayouts                = &descriptorSetLayout;

        VK_TRY(vkCreatePipelineLayout(device, &createInfo, NULL, &pipelineLayout), FATAL("could not create pipeline layout: %s\n", string_VkResult(result)));
    }

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkShaderModule vertexShader   = createShaderModule(readFile("./shaders/vert.spv"));
    VkShaderModule fragmentShader = createShaderModule(readFile("./shaders/frag.spv"));

    VkPipelineShaderStageCreateInfo vertCreateInfo     = { 0 };
    vertCreateInfo.sType                               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertCreateInfo.stage                               = VK_SHADER_STAGE_VERTEX_BIT;
    vertCreateInfo.module                              = vertexShader;
    vertCreateInfo.pName                               = "main";

    VkPipelineShaderStageCreateInfo fragCreateInfo     = { 0 };
    fragCreateInfo.sType                               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragCreateInfo.stage                               = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragCreateInfo.module                              = fragmentShader;
    fragCreateInfo.pName                               = "main";

    VkPipelineShaderStageCreateInfo shaders[]          = { vertCreateInfo, fragCreateInfo }; 

    VkPipelineDynamicStateCreateInfo dynamicState      = { 0 };
    dynamicState.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount                     = ARR_LEN(dynamicStates);
    dynamicState.pDynamicStates                        = dynamicStates;

    VkVertexInputBindingDescription binding            = { 0 };
    binding.binding                                    = 0;
    binding.stride                                     = sizeof(Vertex);
    binding.inputRate                                  = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]    = { 0 };
    attributes[0].binding                              = 0;
    attributes[0].location                             = 0;
    attributes[0].format                               = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset                               = offsetof(Vertex, pos);
    attributes[1].binding                              = 0;
    attributes[1].location                             = 1;
    attributes[1].format                               = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset                               = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput   = { 0 };
    vertexInput.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount          = 1;
    vertexInput.pVertexBindingDescriptions             = &binding;
    vertexInput.vertexAttributeDescriptionCount        = ARR_LEN(attributes);
    vertexInput.pVertexAttributeDescriptions           = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly    = { 0 };
    assembly.sType                                     = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology                                  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable                    = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport         = { 0 };
    viewport.sType                                     = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount                             = 1;
    viewport.scissorCount                              = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer  = { 0 };
    rasterizer.sType                                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable                        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable                 = VK_FALSE;
    rasterizer.polygonMode                             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth                               = 1.0f;
    rasterizer.cullMode                                = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace                               = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable                         = VK_FALSE;
    rasterizer.depthBiasConstantFactor                 = 0.0f;
    rasterizer.depthBiasClamp                          = 0.0f;
    rasterizer.depthBiasSlopeFactor                    = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable                  = VK_FALSE;
    multisampling.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading                     = 1.0f;
    multisampling.pSampleMask                          = NULL;
    multisampling.alphaToCoverageEnable                = VK_FALSE;
    multisampling.alphaToOneEnable                     = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAtt  = { 0 };
    colorBlendAtt.colorWriteMask                       = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAtt.blendEnable                          = VK_FALSE;
    colorBlendAtt.srcColorBlendFactor                  = VK_BLEND_FACTOR_ONE;
    colorBlendAtt.dstColorBlendFactor                  = VK_BLEND_FACTOR_ZERO;
    colorBlendAtt.colorBlendOp                         = VK_BLEND_OP_ADD;
    colorBlendAtt.srcAlphaBlendFactor                  = VK_BLEND_FACTOR_ONE;
    colorBlendAtt.dstAlphaBlendFactor                  = VK_BLEND_FACTOR_ZERO;
    colorBlendAtt.alphaBlendOp                         = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending  = { 0 };
    colorBlending.sType                                = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable                        = VK_FALSE;
    colorBlending.attachmentCount                      = 1;
    colorBlending.pAttachments                         = &colorBlendAtt;

    VkGraphicsPipelineCreateInfo createInfo            = { 0 };
    createInfo.sType                                   = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    createInfo.stageCount                              = 2;
    createInfo.pStages                                 = shaders;
    createInfo.pVertexInputState                       = &vertexInput;
    createInfo.pInputAssemblyState                     = &assembly;
    createInfo.pViewportState                          = &viewport;
    createInfo.pRasterizationState                     = &rasterizer;
    createInfo.pMultisampleState                       = &multisampling;
    createInfo.pDepthStencilState                      = NULL;
    createInfo.pColorBlendState                        = &colorBlending;
    createInfo.pDynamicState                           = &dynamicState;
    createInfo.layout                                  = pipelineLayout;
    createInfo.renderPass                              = renderPass;
    createInfo.subpass                                 = 0;

    VK_TRY(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &graphicsPipeline), FATAL("could not create graphics pipeline: %s\n", string_VkResult(result)));

    vkDestroyShaderModule(device, vertexShader, NULL);
    vkDestroyShaderModule(device, fragmentShader, NULL);
}


static void createFramebuffers(void)
{
    arrsetlen(swapchainFramebuffers, arrlen(swapchainImageViews));

    for (int i = 0; i < arrlen(swapchainImageViews); i++)
    {
        VkFramebufferCreateInfo createInfo = { 0 };
        createInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass              = renderPass;
        createInfo.attachmentCount         = 1;
        createInfo.pAttachments            = &swapchainImageViews[i];
        createInfo.width                   = swapchainExtent.width;
        createInfo.height                  = swapchainExtent.height;
        createInfo.layers                  = 1;

        VK_TRY(vkCreateFramebuffer(device, &createInfo, NULL, &swapchainFramebuffers[i]), FATAL("could not create framebuffer: %s\n", string_VkResult(result)));
    }
}


static inline void createCommandPool(void)
{
    VkCommandPoolCreateInfo createInfo = { 0 };
    createInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex        = graphicsFamilyIndex;

    VK_TRY(vkCreateCommandPool(device, &createInfo, NULL, &commandPool), FATAL("could not create command pool: %s\n", string_VkResult(result)));
}


static inline void allocateCommandBuffers(void)
{
    VkCommandBufferAllocateInfo allocateInfo = { 0 };
    allocateInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool                 = commandPool;
    allocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount          = FRAMES_IN_FLIGHT;

    VK_TRY(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers), FATAL("could not allocate command buffer: %s\n", string_VkResult(result)));
}


static uint32_t findMemoryTypeIndex(uint32_t typeFilter, VkMemoryPropertyFlags propertyFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if (typeFilter & (1 << i) && (memoryProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) return i;
    }

    FATAL("could not find suitable memory type!\n");
}

// TODO: create a buffer memory allocator to prevent allocating many individual memory segments
static void createBuffer(VkDeviceSize size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer *buffer, VkDeviceMemory *bufferMemory)
{
    VkBufferCreateInfo createInfo  = { 0 };
    createInfo.sType               = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size                = size;
    createInfo.usage               = usageFlags;
    createInfo.sharingMode         = VK_SHARING_MODE_EXCLUSIVE;

    VK_TRY(vkCreateBuffer(device, &createInfo, NULL, buffer), FATAL("could not create buffer: %s\n", string_VkResult(result)));

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device, *buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = memoryRequirements.size;
    allocInfo.memoryTypeIndex      = findMemoryTypeIndex(memoryRequirements.memoryTypeBits, propertyFlags);

    VK_TRY(vkAllocateMemory(device, &allocInfo, NULL, bufferMemory), FATAL("could not allocate buffer memory: %s\n", string_VkResult(result)));

    vkBindBufferMemory(device, *buffer, *bufferMemory, 0);

    INFO("created buffer (%lld B)\n", size);
}

static void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *memory)
{
    VkImageCreateInfo createInfo = { 0 };
    createInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createInfo.imageType         = VK_IMAGE_TYPE_2D;
    createInfo.extent.width      = width;
    createInfo.extent.height     = height;
    createInfo.extent.depth      = 1;
    createInfo.mipLevels         = 1;
    createInfo.arrayLayers       = 1;
    createInfo.format            = format;
    createInfo.tiling            = tiling;
    createInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    createInfo.usage             = usage;
    createInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.samples           = VK_SAMPLE_COUNT_1_BIT;

    VK_TRY(vkCreateImage(device, &createInfo, NULL, &textureImage), FATAL("could not create image: %s\n", string_VkResult(result)));

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(device, textureImage, &memoryRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = memoryRequirements.size;
    allocInfo.memoryTypeIndex      = findMemoryTypeIndex(memoryRequirements.memoryTypeBits, properties);

    VK_TRY(vkAllocateMemory(device, &allocInfo, NULL, memory), FATAL("could not allocate image memory: %s\n", string_VkResult(result)));

    vkBindImageMemory(device, *image, *memory, 0);
}

static inline void createTextureImage(void)
{
    int width, height, channels;

    stbi_uc *pixels = stbi_load("./assets/texture.jpg", &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) FATAL("could not load texture image\n");

    VkDeviceSize size = width * height * 4;

    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, size, 0, &data);
    memcpy(data, pixels, size);
    vkUnmapMemory(device, stagingBufferMemory);

    stbi_image_free(pixels);

    createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &textureImage, &textureImageMemory);
}


static void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    // TODO: consider creating own command pool
    VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool                 = commandPool;
    allocInfo.commandBufferCount          = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo    = { 0 };
    beginInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags                       = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion = { 0 };
    copyRegion.size         = size;

    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo       = { 0 };
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

static inline void createVertexBuffer(void)
{
    VkDeviceSize size = sizeof(vertices);

    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, size, 0, &data);
    memcpy(data, vertices, size);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertexBuffer, &vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, size);

    vkDestroyBuffer(device, stagingBuffer, NULL);
    vkFreeMemory(device, stagingBufferMemory, NULL);
}


static inline void createIndexBuffer(void)
{
    VkDeviceSize size = sizeof(indices);

    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, size, 0, &data);
    memcpy(data, indices, size);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indexBuffer, &indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, size);

    vkDestroyBuffer(device, stagingBuffer, NULL);
    vkFreeMemory(device, stagingBufferMemory, NULL);
}


static inline void createUniformBuffers(void)
{
    VkDeviceSize size = sizeof(UniformBufferObject);

    // TODO: merge to a single buffer with offsets
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers[i], &uniformBufferMemories[i]);
        vkMapMemory(device, uniformBufferMemories[i], 0, size, 0, &mappedUniformBuffers[i]);
    }
}


static inline void createDescriptorPool(void)
{
    VkDescriptorPoolSize poolSize         = { 0 };
    poolSize.type                         = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount              = FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo createInfo = { 0 };
    createInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    createInfo.poolSizeCount              = 1;
    createInfo.pPoolSizes                 = &poolSize;
    createInfo.maxSets                    = FRAMES_IN_FLIGHT;

    VK_TRY(vkCreateDescriptorPool(device, &createInfo, NULL, &descriptorPool), FATAL("could not create descriptor pool: %s\n", string_VkResult(result)));
}


static inline void allocateDescriptorSets(void)
{
    // TODO: maybe try and optimize this? really doesn't matter though
    VkDescriptorSetLayout layouts[FRAMES_IN_FLIGHT];
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) layouts[i] = descriptorSetLayout;

    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool              = descriptorPool;
    allocInfo.descriptorSetCount          = FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts                 = layouts;

    VK_TRY(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets), FATAL("could not allocate descriptor sets: %s\n", string_VkResult(result)));

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo    = { 0 };
        bufferInfo.buffer                    = uniformBuffers[i];
        bufferInfo.offset                    = 0;
        bufferInfo.range                     = sizeof(UniformBufferObject);

        VkWriteDescriptorSet writeDescriptor = { 0 };
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.dstSet               = descriptorSets[i];
        writeDescriptor.dstBinding           = 0;
        writeDescriptor.dstArrayElement      = 0;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptor.descriptorCount      = 1;
        writeDescriptor.pBufferInfo          = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &writeDescriptor, 0, NULL);
    }
}


static inline void createSyncObjects(void)
{
    VkSemaphoreCreateInfo semaphoreCreateInfo = { 0 };
    semaphoreCreateInfo.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceCreateInfo         = { 0 };
    fenceCreateInfo.sType                     = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags                     = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        VK_TRY(vkCreateSemaphore(device, &semaphoreCreateInfo, NULL, &imageAvailableSemaphores[i]), FATAL("could not create semaphore: %s\n", string_VkResult(result)));
        VK_TRY(vkCreateSemaphore(device, &semaphoreCreateInfo, NULL, &renderFinishedSemaphores[i]), FATAL("could not create semaphore: %s\n", string_VkResult(result)));
        VK_TRY(vkCreateFence(device, &fenceCreateInfo, NULL, &inFlightFences[i]), FATAL("could not create fence: %s\n", string_VkResult(result)));
    }
}


static void cleanupSwapchain(void)
{
    for (int i = 0; i < arrlen(swapchainFramebuffers); i++)
    {
        vkDestroyFramebuffer(device, swapchainFramebuffers[i], NULL);
    }

    for (int i = 0; i < arrlen(swapchainImageViews); i++)
    {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }

    vkDestroySwapchainKHR(device, swapchain, NULL);
}

static void recreateSwapchain(void)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    // FIXME: suboptimal: we are waiting for rendering to end to recreate the swapchain
    //        see https://vulkan-tutorial.com/Drawing_a_triangle/Swap_chain_recreation (note under "Recreating the swap chain")
    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createFramebuffers();

    INFO("recreated swapchain\n");
}


static inline void updateUniformBuffer(uint32_t currentFrame)
{
    static double angle = 0;
    angle += deltaTime;

    UniformBufferObject ubo;

    glm_mat4_identity(ubo.model);
    glm_rotate(ubo.model, angle * glm_rad(90.0f), (vec3){ 0.0f, 0.0f, 1.0f });

    glm_lookat((vec3){ 0.0f, 0.0f, 2.0f }, (vec3){ 0.0f, 0.0f, 0.0f }, (vec3){ 0.0f, 1.0f, 0.0f }, ubo.view);

    glm_perspective(glm_rad(45.0f), (float) swapchainExtent.width / (float) swapchainExtent.height, 0.1f, 10.0f, ubo.proj);

    memcpy(mappedUniformBuffers[currentFrame], &ubo, sizeof(ubo));
}

static inline void drawFrame(void)
{
    VkCommandBuffer commandBuffer           = commandBuffers[currentFrame];
    VkFence         inFlightFence           = inFlightFences[currentFrame];
    VkSemaphore     imageAvailableSemaphore = imageAvailableSemaphores[currentFrame];
    VkSemaphore     renderFinishedSemaphore = renderFinishedSemaphores[currentFrame];

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VK_TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex), {
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapchain();
            return;
        }

        if (result == VK_SUBOPTIMAL_KHR) WARN("drawing onto suboptimal image\n");
        else FATAL("could not acquire image for drawing: %s\n", string_VkResult(result));
    });

    vkResetFences(device, 1, &inFlightFence);

    vkResetCommandBuffer(commandBuffer, 0);

    VkClearValue clearColor = (VkClearValue){{{ 0.0f, 0.0f, 0.0f, 1.0f }}};

    {
        VkCommandBufferBeginInfo beginInfo = { 0 };
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        VK_TRY(vkBeginCommandBuffer(commandBuffer, &beginInfo), FATAL("could not begin command buffer: %s\n", string_VkResult(result)));
    }

    {
        VkRenderPassBeginInfo beginInfo    = { 0 };
        beginInfo.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass               = renderPass;
        beginInfo.framebuffer              = swapchainFramebuffers[imageIndex];
        beginInfo.renderArea.offset        = (VkOffset2D){ 0, 0 };
        beginInfo.renderArea.extent        = swapchainExtent;
        beginInfo.clearValueCount          = 1;
        beginInfo.pClearValues             = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);

    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    VkViewport viewport = { 0 };
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float) swapchainExtent.width;
    viewport.height     = (float) swapchainExtent.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor    = { 0 };
    scissor.offset      = (VkOffset2D){ 0, 0 };
    scissor.extent      = swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, NULL);

    vkCmdDrawIndexed(commandBuffer, ARR_LEN(indices), 1, 0, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    VK_TRY(vkEndCommandBuffer(commandBuffer), FATAL("could not record command buffer: %s\n", string_VkResult(result)));

    updateUniformBuffer(currentFrame);

    VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo         = { 0 };
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask    = &stageFlags;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderFinishedSemaphore;

    VK_TRY(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence), FATAL("could not submit draw command buffer: %s\n", string_VkResult(result)));

    VkPresentInfoKHR presentInfo    = { 0 };
    presentInfo.sType               = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount  = 1;
    presentInfo.pWaitSemaphores     = &renderFinishedSemaphore;
    presentInfo.swapchainCount      = 1;
    presentInfo.pSwapchains         = &swapchain;
    presentInfo.pImageIndices       = &imageIndex;

    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapchain();
    }
    else if (result != VK_SUCCESS) FATAL("could not present swapchain image: %s\n", string_VkResult(result));

    currentFrame = (currentFrame + 1) % FRAMES_IN_FLIGHT;

    // TODO: error handling
    struct timespec currentClock;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &currentClock);
    deltaTime = (currentClock.tv_sec - lastFrameEnd.tv_sec) + 1.0e-9 * (currentClock.tv_nsec - lastFrameEnd.tv_nsec);
    lastFrameEnd = currentClock;
}


static inline void cleanup(void)
{
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(device, renderFinishedSemaphores[i], NULL);
        vkDestroyFence(device, inFlightFences[i], NULL);

        vkDestroyBuffer(device, uniformBuffers[i], NULL);
        vkFreeMemory(device, uniformBufferMemories[i], NULL);
    }

    cleanupSwapchain();

    arrfree(swapchainImages);
    arrfree(swapchainImageViews);
    arrfree(swapPresentModes);
    arrfree(swapFormats);

    vkDestroyCommandPool(device, commandPool, NULL);
    vkDestroyBuffer(device, vertexBuffer, NULL);
    vkFreeMemory(device, vertexBufferMemory, NULL);
    vkDestroyBuffer(device, indexBuffer, NULL);
    vkFreeMemory(device, indexBufferMemory, NULL);
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    glfwDestroyWindow(window);
    glfwTerminate();
}


int main(void)
{
    createWindow();
    createVulkanInstance();
    createWindowSurface();
    findSuitableGPU();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    allocateCommandBuffers();
    createTextureImage();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    allocateDescriptorSets();
    createSyncObjects();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        drawFrame();
    }

    vkDeviceWaitIdle(device);
    cleanup();

    return 0;
}