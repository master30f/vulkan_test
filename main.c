#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include "glfw3.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"


#define TITLE            "Vulkan test"
#define WINDOW_WIDTH     1280
#define WINDOW_HEIGHT    720


#define QFI_GRAPHICS_BIT 0b01
#define QFI_PRESENT_BIT  0b10
#define QFI_COMPLETE     (QFI_GRAPHICS_BIT | QFI_PRESENT_BIT)

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


// TODO: support more validation layers
const char *validationLayer = "VK_LAYER_KHRONOS_validation";

const char *deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
const uint32_t deviceExtensionsCount = sizeof(deviceExtensions)/sizeof(char *);

GLFWwindow *window;
VkDevice device;


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


void enableValidationLayers(VkInstanceCreateInfo *createInfo)
{
    // TODO: change all of these dynamic arrays to buffers
    uint32_t layersCount      = 0;
    VkLayerProperties *layers = NULL;
    VK_TRY(vkEnumerateInstanceLayerProperties(&layersCount, NULL), {
        ERROR("could not get available validation layers: %s", string_VkResult(result));
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

bool physicalDeviceSupportsSurfaceKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface)
{
    VkBool32 supports = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &supports);
    return supports;
}

VkSurfaceFormatKHR selectSwapSurfaceFormat(const VkSurfaceFormatKHR *availableFormats)
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

VkPresentModeKHR selectSwapPresentMode(const VkPresentModeKHR *availableModes)
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

VkExtent2D selectSwapExtent(const VkSurfaceCapabilitiesKHR *capabilities)
{
    if (capabilities->currentExtent.width != UINT32_MAX) return capabilities->currentExtent;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return (VkExtent2D){
        .width  = clamp(width, capabilities->minImageExtent.width, capabilities->maxImageExtent.width),
        .height = clamp(height, capabilities->minImageExtent.height, capabilities->maxImageExtent.height)
    };
}

VkShaderModule createShaderModule(ByteBuf code)
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


int main(void)
{
    // create window

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, TITLE, NULL, NULL);

    // create vulkan instance

    VkInstance instance;

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

    // create window surface

    VkSurfaceKHR surface;
    VK_TRY(glfwCreateWindowSurface(instance, window, NULL, &surface), FATAL("cound not create window surface: %s\n", string_VkResult(result)));

    // find suitable GPU

    VkPhysicalDevice physicalDevice        = VK_NULL_HANDLE;

    uint32_t graphicsFamilyIndex           = 0;
    uint32_t presentFamilyIndex            = 0;

    VkSurfaceCapabilitiesKHR swapCapabilities;
    VkPresentModeKHR *swapPresentModes     = NULL;
    VkSurfaceFormatKHR *swapFormats        = NULL;

    {
        uint32_t devicesCount                  = 0;
        VkPhysicalDevice *devices              = NULL;
        VK_TRY(vkEnumeratePhysicalDevices(instance, &devicesCount, NULL), FATAL("could not get physical devices: %s\n", string_VkResult(result)));
        arrsetlen(devices, devicesCount);
        vkEnumeratePhysicalDevices(instance, &devicesCount, devices);

        uint32_t queueFamiliesCount            = 0;
        VkQueueFamilyProperties *queueFamilies = NULL;

        uint32_t extensionsCount               = 0;
        VkExtensionProperties *extensions      = NULL;

        for (uint32_t i = 0; i < devicesCount; i++)
        {
            VkPhysicalDevice device = devices[i];

            // check queue families

            uint8_t QFIBitmap = 0;

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

                if (QFIBitmap == QFI_COMPLETE) break;
            }

            if (QFIBitmap != QFI_COMPLETE) continue;

            // check extensions

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

            if (foundExtensions != deviceExtensionsCount) continue;

            // check swapchain capabilities

            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &swapCapabilities);

            uint32_t swapFormatsCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &swapFormatsCount, NULL);
            if (swapFormatsCount == 0) continue;
            arrsetlen(swapFormats, swapFormatsCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &swapFormatsCount, swapFormats);

            uint32_t swapPresentModesCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &swapPresentModesCount, NULL);
            if (swapPresentModesCount == 0) continue;
            arrsetlen(swapPresentModes, swapPresentModesCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &swapPresentModesCount, swapPresentModes);

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

    // create logical device

    {
        float queuePriority                     = 1.0f;

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

        VkQueue graphicsQueue;
        VkQueue presentQueue;
        vkGetDeviceQueue(device, graphicsFamilyIndex, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamilyIndex, 0, &presentQueue);
    }

    // create swap chain

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    VkImage *swapchainImages = NULL;

    {
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

        VkSwapchainCreateInfoKHR createInfo = { 0 };
        createInfo.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface                  = surface;
        createInfo.minImageCount            = imageCount;
        createInfo.imageFormat              = surfaceFormat.format;
        createInfo.imageColorSpace          = surfaceFormat.colorSpace;
        createInfo.imageExtent              = swapchainExtent;
        createInfo.imageArrayLayers         = 1;
        createInfo.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.preTransform             = swapCapabilities.currentTransform;
        createInfo.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode              = presentMode;
        createInfo.clipped                  = VK_TRUE;
        createInfo.oldSwapchain             = VK_NULL_HANDLE;

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

    // create image views

    VkImageView *swapchainImageViews = NULL;

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

            VK_TRY(vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]), FATAL("could not create swap image view: %s", string_VkResult(result)));
        }

        INFO("created %lld swapchain image views\n", arrlen(swapchainImageViews));
    }

    // create render pass

    VkRenderPass renderPass;

    {
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
        createInfo.attachmentCount               = 1;
        createInfo.pAttachments                  = &colorAttachment;
        createInfo.subpassCount                  = 1;
        createInfo.pSubpasses                    = &subpass;

        VK_TRY(vkCreateRenderPass(device, &createInfo, NULL, &renderPass), FATAL("could not create render pass: %s", string_VkResult(result)));
    }

    // create graphics pipeline

    VkPipelineLayout pipelineLayout;

    {
        {
            VkPipelineLayoutCreateInfo createInfo = { 0 };
            createInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

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
        dynamicState.dynamicStateCount                     = sizeof(dynamicStates)/sizeof(VkDynamicState);
        dynamicState.pDynamicStates                        = dynamicStates;

        VkPipelineVertexInputStateCreateInfo vertexInput   = { 0 };
        vertexInput.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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



        vkDestroyShaderModule(device, vertexShader, NULL);
        vkDestroyShaderModule(device, fragmentShader, NULL);
    }

    // main loop

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    // cleanup

    for (int i = 0; i < arrlen(swapchainImageViews); i++)
    {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }

    arrfree(swapchainImages);
    arrfree(swapchainImageViews);
    arrfree(swapPresentModes);
    arrfree(swapFormats);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}