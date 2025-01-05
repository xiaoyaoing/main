#pragma once

#include "Core/Vulkan.h"
#include "Core/Images/ImageView.h"
#include "RenderTarget.h"
#include "Pipeline.h"
#include "RenderGraph/RenderGraph.h"
#include "ResourceBindingState.h"
#include "Scene/Scene.h"
#include "Core/BufferPool.h"
#include "Images/VirtualViewport.h"

class Device;
class SwapChain;
class Window;
class FrameBuffer;
class Sampler;
class Scene;
class View;

struct FrameResource {
    static constexpr uint32_t BUFFER_POOL_BLOCK_SIZE = 256;

    const std::unordered_map<VkBufferUsageFlags, uint32_t> supported_usage_map = {
        {VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 1},
        {VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2},
        {VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 1},
        {VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 1}
    };

    FrameResource(Device&);
    ~FrameResource() = default;

    void reset();

    std::unique_ptr<CommandBuffer> graphicCommandBuffer{nullptr};
    std::unordered_map<VkBufferUsageFlags, std::unique_ptr<BufferPool>> bufferPools{};
};

struct alignas(16) GlobalUniform {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 inv_view_proj;
    glm::mat4 mvp;
};

enum class UniformBindingPoints : uint8_t {
    PER_VIEW = 0,
    PER_RENDERABLE = 1,
    PRIM_INFO = 2,
    PER_RENDERABLE_MORPHING = 3,
    LIGHTS = 4,
    SHADOW = 5,
    MATERIALS = 6,
    FROXELS = 7,
    PER_MATERIAL_INSTANCE = 8
};

enum class SamplerBinding {};

enum class DescriptorSetPoints : uint32_t {
    UNIFORM = 0,
    SAMPLER = 1,
    INPUT = 2,
    ACCELERATION = 3,
    COUNT = 4
};

class RenderContext {
public:
    RenderContext(Device& device, VkSurfaceKHR surface, Window& window);

    RenderContext& bindBuffer(uint32_t binding, const Buffer& buffer, VkDeviceSize offset = 0, VkDeviceSize range = 0, uint32_t setId = -1, uint32_t array_element = 0);
    RenderContext& bindAcceleration(uint32_t binding, const Accel& acceleration, uint32_t setId = -1, uint32_t array_element = 0);
    RenderContext& bindImageSampler(uint32_t binding, const ImageView& view, const Sampler& sampler, uint32_t setId = -1, uint32_t array_element = 0);
    RenderContext& bindImage(uint32_t binding, const ImageView& view, uint32_t setId = -1, uint32_t array_element = 0);
    RenderContext& bindSampler(uint32_t binding, const Sampler& sampler, uint32_t setId = -1, uint32_t array_element = 0);
    RenderContext& bindView(const View& view);
    RenderContext& bindPrimitiveGeom(CommandBuffer& commandBuffer, const Primitive& primitive);
    RenderContext& bindScene(CommandBuffer& commandBuffer, const Scene& scene);
    RenderContext& bindPrimitiveShading(CommandBuffer& commandBuffer, const Primitive& primitive);

    template<typename T>
    RenderContext& bindLight(const std::vector<SgLight>& lights, uint32_t setId, uint32_t binding);

    template<typename T>
    RenderContext& bindPushConstants(const T& pushConstants) {
        return bindPushConstants(toBytes(pushConstants));
    }

    template<>
    RenderContext& bindPushConstants<std::vector<uint8_t>>(const std::vector<uint8_t>& pushConstants);

    RenderContext& bindShaders(const ShaderPipelineKey& shaderKeys);
    void flushPipelineState(CommandBuffer& commandBuffer);
    void flushPushConstantStage(CommandBuffer& commandBuffer);
    void clearPassResources();
    const std::unordered_map<uint32_t, ResourceSet>& getResourceSets() const;
    VkPipelineBindPoint getPipelineBindPoint() const;
    VkExtent2D getViewPortExtent() const;
    VkExtent3D getViewPortExtent3D() const;
    VkExtent2D getSwapChainExtent() const;
    uint32_t getSwapChainImageCount() const;
    void prepare();
    void beginFrame();
    void waitFrame();
    uint32_t getActiveFrameIndex() const;
    void submitAndPresent(CommandBuffer& commandBuffer, VkFence fence = VK_NULL_HANDLE);
    void submit(CommandBuffer& commandBuffer, bool waiteFence = true, VkQueueFlagBits queueFlags = VK_QUEUE_GRAPHICS_BIT);
    void setActiveFrameIdx(int idx);
    bool isPrepared() const;
    PipelineState& getPipelineState();
    SgImage& getCurHwtexture();
    SgImage& getSwapChainImage();
    void flushDescriptorState(CommandBuffer& commandBuffer, VkPipelineBindPoint pipeline_bind_point);
    void flushAndDrawIndexed(CommandBuffer& commandBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t vertexOffset = 0, uint32_t firstInstance = 0);
    void flushAndDraw(CommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void traceRay(CommandBuffer& commandBuffer, VkExtent3D dims);
    void flushAndDispatch(CommandBuffer& commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    void flushAndDispatchIndirect(CommandBuffer& commandBuffer, Buffer& buffer, VkDeviceSize offset);
    void flushAndDrawMeshTasks(CommandBuffer& commandBuffer, uint groupCountX, uint groupCountY, uint groupCountZ);
    void flushAndDrawMeshTasksIndirect(CommandBuffer& commandBuffer, Buffer& buffer, VkDeviceSize offset);
    void beginRenderPass(CommandBuffer& commandBuffer, RenderTarget& renderTarget, const std::vector<SubpassInfo>& subpassInfos);
    void nextSubpass(CommandBuffer& commandBuffer);
    void flush(CommandBuffer& commandBuffer);
    void endRenderPass(CommandBuffer& commandBuffer, RenderTarget& renderTarget);
    void resetViewport(CommandBuffer& commandBuffer);
    BufferAllocation allocateBuffer(VkDeviceSize allocateSize, VkBufferUsageFlags usage);
    CommandBuffer& getGraphicCommandBuffer();
    CommandBuffer& getComputeCommandBuffer();
    Device& getDevice();
    void handleSurfaceChanges();
    void recrateSwapChain(VkExtent2D extent);
    void copyBuffer(const Buffer& src, Buffer& dst);
    void setFlipViewport(bool flip);
    bool getFlipViewport() const;

private:
    bool frameActive = false;
    VkSemaphore acquiredSem;
    bool prepared{false};
    uint32_t activeFrameIndex{0};
    Device& device;
    std::unique_ptr<SwapChain> swapchain;
    std::unique_ptr<VirtualViewport> virtualViewport;
    uint32_t swapChainCount{0};
    std::vector<std::unique_ptr<FrameBuffer>> frameBuffers;
    VkExtent2D surfaceExtent;

    struct {
        VkSemaphore presentFinishedSem;
        VkSemaphore renderFinishedSem;
        VkSemaphore computeFinishedSem;
        VkSemaphore graphicFinishedSem;
    } semaphores;

    PipelineState pipelineState;
    std::vector<std::unique_ptr<FrameResource>> frameResources{};
    std::vector<SgImage> hwTextures;
    std::unordered_map<uint32_t, DescriptorLayout*> descriptor_set_layout_binding_state;
    std::unordered_map<uint32_t, ResourceSet> resourceSets;
    bool resourceSetsDirty{false};
    std::vector<uint8_t> storePushConstants;
    uint32_t maxPushConstantSize;
    bool flipViewport = true;
};

extern RenderContext* g_context;

template<typename T>
RenderContext& RenderContext::bindLight(const std::vector<SgLight>& lights, uint32_t setId, uint32_t binding) {
    binding = static_cast<uint32_t>(UniformBindingPoints::LIGHTS);
    const auto buffer = allocateBuffer(sizeof(T), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    std::vector<LightUib> directionalLights, pointLights, spotLights;

    for (const auto& light : lights) {
        LightUib lightUBO{};
        switch (light.type) {
            case LIGHT_TYPE::Point:
                lightUBO = {
                    .color = glm::vec4(light.lightProperties.color, light.lightProperties.intensity),
                    .position = glm::vec4(light.lightProperties.position, 1),
                    .direction = glm::vec4(0.0f),
                    .info = glm::vec4(0.0f)
                };
                pointLights.push_back(lightUBO);
            default:
                break;
        }
    }

    T lightUbos{};
    std::copy(directionalLights.begin(), directionalLights.end(), lightUbos.directionalLights);
    std::copy(pointLights.begin(), pointLights.end(), lightUbos.pointLights);
    std::copy(spotLights.begin(), spotLights.end(), lightUbos.spotLights);

    buffer.buffer->uploadData(&lightUbos, sizeof(T), buffer.offset);
    bindBuffer(binding, *buffer.buffer, buffer.offset, buffer.size, setId, 0);

    return *this;
}
