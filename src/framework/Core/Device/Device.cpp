#include "GLFW/glfw3.h"
#include "Device.h"
#include "Common/DebugUtils.h"
#include "Core/Queue.h"
#include "Core/CommandPool.h"

#define VMA_IMPLEMENTATION

#include "vk_mem_alloc.h"
#include "Common/ResourceCache.h"

const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

Device* g_device = nullptr;

Device::~Device() {
    delete cache;
}

bool Device::isImageFormatSupported(VkFormat format) {
    VkImageFormatProperties format_properties;

    auto result = vkGetPhysicalDeviceImageFormatProperties(_physicalDevice,
                                                           format,
                                                           VK_IMAGE_TYPE_2D,
                                                           VK_IMAGE_TILING_OPTIMAL,
                                                           VK_IMAGE_USAGE_SAMPLED_BIT,
                                                           0,// no create flags
                                                           &format_properties);
    return result != VK_ERROR_FORMAT_NOT_SUPPORTED;
}

Device::Device(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkInstance instance, std::unordered_map<const char*, bool> requiredExtensions) {
    _physicalDevice = physicalDevice;

    vkGetPhysicalDeviceProperties(_physicalDevice, &properties);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(queueFamilyCount);
    std::vector<std::vector<float>> queuePrioritys(queueFamilyCount);

    // todo support queuePriority
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++) {
        const auto&             queueProp = queueFamilyProperties[queueFamilyIndex];
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount       = queueProp.queueCount;
        queuePrioritys[queueFamilyIndex].resize(queueProp.queueCount, 0.5);
        queueCreateInfo.pQueuePriorities = queuePrioritys[queueFamilyIndex].data();
        queueCreateInfos.push_back(queueCreateInfo);
    }

    uint32_t device_extension_count;
    VK_CHECK_RESULT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &device_extension_count, nullptr));
    deviceExtensions = std::vector<VkExtensionProperties>(device_extension_count);
    VK_CHECK_RESULT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &device_extension_count, deviceExtensions.data()));

    std::vector<const char*> unsupported_extensions{};
    for (auto& extension : requiredExtensions) {
        if (isExtensionSupported(extension.first)) {
            enabled_extensions.emplace_back(extension.first);
        } else {
            unsupported_extensions.emplace_back(extension.first);
        }
    }

    if (enabled_extensions.size() > 0) {
        LOGI("Device supports the following requested extensions:");
        for (auto& extension : enabled_extensions) {
            LOGI("  \t{}", extension);
        }
    }

    if (unsupported_extensions.size() > 0) {
        auto error = false;
        for (auto& extension : unsupported_extensions) {
            auto extension_is_optional = requiredExtensions[extension];
            if (extension_is_optional) {
                LOGW("Optional device extension {} not available, some features may be disabled", extension);
            } else {
                LOGE("Required device extension {} not available, cannot run", extension);
                error = VK_TRUE;
            }
        }

        if (error) {
            std::runtime_error("Required extension missed");
        }
    }

    enabled_extensions.push_back("VK_KHR_get_memory_requirements2");
    enabled_extensions.push_back("VK_KHR_dedicated_allocation");

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    // pointers to the queue creation info and device features structs
    createInfo.pQueueCreateInfos    = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = queueCreateInfos.size();
    createInfo.pEnabledFeatures     = nullptr;
    // enable the device extensions todo checkExtension supported
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(enabled_extensions.size());
    createInfo.ppEnabledExtensionNames = enabled_extensions.data();

    // Display supported extensions
    if (!enabled_extensions.empty()) {
        LOGI("Device use the following extensions:");
        for (auto& extension : enabled_extensions) {
            LOGI("  \t{}", extension);
        }
    }

    auto enableExtension = [this](const char* extensionName) {
        return std::find_if(enabled_extensions.begin(), enabled_extensions.end(), [extensionName](const char* extension) {
                   return strcmp(extensionName, extension) == 0;
               }) != enabled_extensions.end();
    };
    VkPhysicalDeviceFeatures2 device_features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    //todo check if this is needed
    device_features2.features.geometryShader    = VK_TRUE;
    device_features2.features.shaderInt64       = VK_TRUE;
    device_features2.features.multiViewport     = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.runtimeDescriptorArray           = VK_TRUE;
    features12.bufferDeviceAddress              = VK_TRUE;
    features12.descriptorIndexing               = VK_TRUE;

    VkPhysicalDeviceSynchronization2FeaturesKHR syncronization2_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};

    syncronization2_features.synchronization2 = VK_TRUE;

    features12.pNext       = &syncronization2_features;
    device_features2.pNext = &features12;

    void** nextChain = &syncronization2_features.pNext;

    if (enableExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
        VkPhysicalDeviceMeshShaderFeaturesEXT enabledMeshShaderFeatures{};
        enabledMeshShaderFeatures.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        enabledMeshShaderFeatures.meshShader = VK_TRUE;
        enabledMeshShaderFeatures.taskShader = VK_TRUE;
        enabledMeshShaderFeatures.pNext      = nullptr;
        *nextChain                           = &enabledMeshShaderFeatures;
        nextChain                            = &enabledMeshShaderFeatures.pNext;
        LOGI("Enabled Mesh Shader Extension");
    }

    if (enableExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_fts{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_fts{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_fts{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT};

        VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering                 = VK_TRUE;
        features13.synchronization2                 = VK_TRUE;
        features13.maintenance4                     = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};

        VkPhysicalDeviceMaintenance4FeaturesKHR maintenance4_fts = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR};

        atomic_fts.shaderBufferFloat32AtomicAdd              = VK_TRUE;
        atomic_fts.shaderBufferFloat32Atomics                = VK_TRUE;
        atomic_fts.shaderSharedFloat32AtomicAdd              = VK_TRUE;
        atomic_fts.shaderSharedFloat32Atomics                = VK_TRUE;
        atomic_fts.pNext                                     = nullptr;
        accel_fts.accelerationStructure                      = VK_TRUE;
        accel_fts.pNext                                      = &atomic_fts;
        rt_fts.rayTracingPipeline                            = VK_TRUE;
        rt_fts.pNext                                         = &accel_fts;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.scalarBlockLayout                         = VK_TRUE;

        dynamic_rendering_feature.dynamicRendering = VK_TRUE;
        maintenance4_fts.maintenance4              = VK_TRUE;
        *nextChain                                 = &maintenance4_fts;

        maintenance4_fts.pNext          = &dynamic_rendering_feature;
        dynamic_rendering_feature.pNext = &rt_fts;

        device_features2.features.samplerAnisotropy              = VK_TRUE;
        device_features2.features.vertexPipelineStoresAndAtomics = VK_TRUE;

        nextChain                                  = &rt_fts.pNext;

        VkPhysicalDeviceProperties2 device_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        device_properties.pNext = &rayTracingPipelineProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &device_properties);
        LOGI("Enabled Ray Tracing Pipeline Extension");
    }

    bool enableFragmentStoresAndAtomics = VK_TRUE;

    if (enableFragmentStoresAndAtomics) {
        device_features2.features.fragmentStoresAndAtomics = VK_TRUE;
    }

    createInfo.pNext = &device_features2;

    VK_CHECK_RESULT(vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device));

    queues.resize(queueFamilyCount);
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, queueFamilyIndex, surface, &presentSupport);
        for (uint32_t i = 0; i < queueCreateInfos[queueFamilyIndex].queueCount; i++) {
            const VkQueueFamilyProperties& queueFamilyProp = queueFamilyProperties[queueFamilyIndex];
            queues[queueFamilyIndex].emplace_back(
                std::move(std::make_unique<Queue>(this, queueFamilyIndex, i, presentSupport, queueFamilyProp)));
        }
    }

    commandPools.emplace(VK_QUEUE_GRAPHICS_BIT, CommandPool(*this, getQueueByFlag(VK_QUEUE_GRAPHICS_BIT, 0).getFamilyIndex(), CommandBuffer::ResetMode::AlwaysAllocate));
    commandPools.emplace(VK_QUEUE_TRANSFER_BIT, CommandPool(*this, getQueueByFlag(VK_QUEUE_TRANSFER_BIT, 0).getFamilyIndex(), CommandBuffer::ResetMode::AlwaysAllocate));
    //Init Cache
    cache = new ResourceCache(*this);
    ResourceCache::initCache(*this);

    //Init Vma
    VmaVulkanFunctions vma_vulkan_func{};
    vma_vulkan_func.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vma_vulkan_func.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice         = physicalDevice;
    allocatorInfo.device                 = _device;
    allocatorInfo.instance               = instance;
    allocatorInfo.pVulkanFunctions       = &vma_vulkan_func;

    if (isExtensionSupported("VK_KHR_get_memory_requirements2") && isExtensionSupported("VK_KHR_dedicated_allocation")) {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
        vma_vulkan_func.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
        vma_vulkan_func.vkGetImageMemoryRequirements2KHR  = vkGetImageMemoryRequirements2KHR;
    }

    if (isExtensionSupported(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) && enableExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator))
}

Queue& Device::getQueueByFlag(VkQueueFlagBits requiredFlag, uint32_t queueIndex) {
    for (const auto& queueFamily : queues) {
        const auto& prop       = queueFamily[0]->getProp();
        auto        queueFlag  = prop.queueFlags;
        auto        queueCount = prop.queueCount;
        if ((queueFlag & requiredFlag) && queueIndex < queueCount)
            return *queueFamily[queueIndex];
    }
    LOGE("Failed to find queue with required flag");
    Queue* queue;
    return *queue;
    // return *(queues[0][0]);
}

const Queue& Device::getPresentQueue(uint32_t queueIndex) {
    for (const auto& queueFamily : queues) {
        const auto& prop       = queueFamily[0]->getProp();
        auto        canPresent = queueFamily[0]->supportPresent();
        auto        queueCount = prop.queueCount;
        if (canPresent && queueIndex < queueCount)
            return *queueFamily[queueIndex];
    }
    RUN_TIME_ERROR("Failed to find present queue");

    Queue* queue;
    return *queue;
    // return *(queues[0][0]);
}

bool Device::isExtensionSupported(const std::string& extensionName) {
    return std::find_if(deviceExtensions.begin(), deviceExtensions.end(), [extensionName](auto& device_extension) {
               return std::strcmp(device_extension.extensionName, extensionName.c_str()) == 0;
           }) != deviceExtensions.end();
}
