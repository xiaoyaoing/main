#pragma once

#include "Vulkan.h"

class CommandBuffer;
class Device;

class Buffer {
public:
    ~Buffer();

    static std::unique_ptr<Buffer> FromBuffer(Device& device, CommandBuffer& command, const Buffer& srcBuffer, VkBufferUsageFlags usageFlags, VkDeviceSize offset = 0,VkDeviceSize size= -1);

    explicit Buffer(Device& device, uint64_t bufferSize, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, const void* data = nullptr);
    Buffer(Buffer&& buffer);
    Buffer(Buffer& rhs)           = delete;
    Buffer operator=(Buffer& rhs) = delete;
    // void   copyFrom(Buffer& srcBuffer);
    void  uploadData(const void* srcData, uint64_t size = -1, uint64_t offset = 0);
    void* map();
    void  unmap();

    template<typename T>
    std::vector<T> getData() {
        std::vector<T> result;
        result.resize(_allocatedSize / sizeof(T));

        void* dstData{nullptr};
        vmaMapMemory(_allocator, _bufferAllocation, &dstData);
        memcpy(result.data(), dstData, _allocatedSize);
        vmaUnmapMemory(_allocator, _bufferAllocation);
        return result;
    }

    VkDeviceSize        getDeviceAddress() const;
    inline VkBuffer     getHandle() const { return _buffer; }
    inline Device&      getDevice() const { return device; }
    inline VkDeviceSize getSize() const { return _allocatedSize; }
    inline VkBufferUsageFlags getUsageFlags() const { return _usageFlags; }
    inline VmaMemoryUsage     getMemoryUsage() const { return _memoryUsage; }
protected:
    VkBuffer      _buffer{nullptr};
    uint32_t      _allocatedSize;
    VmaAllocator  _allocator{nullptr};
    VmaAllocation _bufferAllocation{nullptr};
    Device&       device;
    VkBufferUsageFlags _usageFlags;
    VmaMemoryUsage     _memoryUsage;
};
