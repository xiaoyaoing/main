#include "Vulkan.h"
#include "ImageView.h"

class Image {
    Device *device;
    VkImage image;
    VmaAllocation memory;

    VkExtent3D extent{};

    VkImageType type{};

    VkFormat format{};

    VkImageUsageFlags usage{};

    VkSampleCountFlagBits sample_count{};

    VkImageTiling tiling{};

    VkImageSubresource subresource{};

    uint32_t array_layer_count{0};

    uint8_t *mapped_data{nullptr};


public:
    Image(Device &device,
          const VkExtent3D &extent,
          VkFormat format,
          VkImageUsageFlags image_usage,
          VmaMemoryUsage memory_usage,
          VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT,
          uint32_t mip_levels = 1,
          uint32_t array_layers = 1,
          VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
          VkImageCreateFlags flags = 0,
          uint32_t num_queue_families = 0,
          const uint32_t *queue_families = nullptr);

    Image(Device const &device,
          VkImage handle,
          const VkExtent3D &extent,
          VkFormat format,
          VkImageUsageFlags image_usage,
          VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT);

    inline VkImage getHandle() {
        return _image;
    };

    inline VkFormat getFormat() {
        return _format;
    }

    inline VkImageType getImageType() {
        return _imageType;
    }

    inline VkSampleCountFlagBits getSampleCount() {
        return VK_SAMPLE_COUNT_1_BIT;
    };

    inline VkImageUsageFlags getUseFlags() {
        return _useFlags;
    };

    inline const VkExtent3D &getExtent() {
        return _extent;
    }

    Image(VmaAllocator allocator, VmaMemoryUsage memoryUsage, const VkImageCreateInfo &createInfo);

    static inline VkImageCreateInfo getDefaultImageInfo() {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = nullptr;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return imageInfo;
    }

    void addView(ImageView *pView);
};