//
// Created by 打工人 on 2023/3/30.
//
#include "RenderContext.h"
#include "Core/CommandBuffer.h"
#include "Core/Device/Device.h"
#include "SwapChain.h"
#include "Queue.h"
#include "Core/Images/Image.h"
#include "RenderTarget.h"
#include "View.h"
#include "ctpl_stl.h"
#include "Core/FrameBuffer.h"
#include "Core/Buffer.h"

#include "PlatForm/Window.h"
#include "Common/ResourceCache.h"
#include "Common/VkCommon.h"
#include "IO/ImageIO.h"
#include "Images/ImageUtil.h"
#include "Images/Sampler.h"
#include "RayTracing/Accel.h"
#include "RayTracing/SbtWarpper.h"

#include <unordered_set>

RenderContext* g_context = nullptr;

void FrameResource::reset() {
    for (auto& it : bufferPools)
        it.second->reset();
}

FrameResource::FrameResource(Device& device) {
    for (auto& it : supported_usage_map) {
        bufferPools.emplace(
            it.first,
            std::move(std::make_unique<BufferPool>(device, BUFFER_POOL_BLOCK_SIZE * it.second * 1024, it.first)));
    }
}

RenderContext::RenderContext(Device& device, VkSurfaceKHR surface, Window& window)
    : device(device) {
    swapchain = std::make_unique<SwapChain>(device, surface, window.getExtent());
    if (swapchain) {
        surfaceExtent = swapchain->getExtent();

        VkExtent3D extent{surfaceExtent.width, surfaceExtent.height, 1};

        for (auto& image_handle : swapchain->getImages()) {
            hwTextures.emplace_back(device, image_handle, extent, swapchain->getImageFormat(), swapchain->getUseage());
        }
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK_RESULT(
        vkCreateSemaphore(device.getHandle(), &semaphoreCreateInfo, nullptr, &semaphores.renderFinishedSem));
    VK_CHECK_RESULT(
        vkCreateSemaphore(device.getHandle(), &semaphoreCreateInfo, nullptr, &semaphores.presentFinishedSem));
    VK_CHECK_RESULT(
        vkCreateSemaphore(device.getHandle(), &semaphoreCreateInfo, nullptr, &semaphores.graphicFinishedSem));
    VK_CHECK_RESULT(
        vkCreateSemaphore(device.getHandle(), &semaphoreCreateInfo, nullptr, &semaphores.computeFinishedSem));

    VkSubmitInfo submit_info         = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &semaphores.computeFinishedSem;
    device.getQueueByFlag(VK_QUEUE_GRAPHICS_BIT, 0).submit({submit_info}, VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool        = device.getCommandPool().getHandle();
    allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = getSwapChainImageCount();

    std::vector<VkCommandBuffer> vkGraphicCommandBuffers(getSwapChainImageCount());
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device.getHandle(), &allocateInfo, vkGraphicCommandBuffers.data()))

    for (uint32_t i = 0; i < getSwapChainImageCount(); i++) {
        frameResources.emplace_back(std::make_unique<FrameResource>(device));
        frameResources.back()->graphicCommandBuffer = std::make_unique<CommandBuffer>(device.getHandle(), device.getCommandPool().getHandle(), vkGraphicCommandBuffers[i], VK_QUEUE_GRAPHICS_BIT);
    }

    maxPushConstantSize = device.getProperties().limits.maxPushConstantsSize;
    virtualViewport     = std::make_unique<VirtualViewport>(device, VkExtent2D{1920, 1080}, getSwapChainImageCount());
}

void RenderContext::beginFrame() {
    if (swapchain) {
        VK_CHECK_RESULT(swapchain->acquireNextImage(activeFrameIndex, semaphores.presentFinishedSem, VK_NULL_HANDLE));
    }
    frameActive = true;

    frameResources[activeFrameIndex]->reset();
    clearPassResources();

    auto& commandBuffer = getGraphicCommandBuffer();
    commandBuffer.beginRecord(0);

    commandBuffer.setViewport(0, {vkCommon::initializers::viewport(float(getViewPortExtent().width), float(getViewPortExtent().height), 0.0f, 1.0f, flipViewport)});
    commandBuffer.setScissor(0, {vkCommon::initializers::rect2D(float(getViewPortExtent().width), float(getViewPortExtent().height), 0, 0)});
}

void RenderContext::waitFrame() {
}

void RenderContext::prepare() {
}

uint32_t RenderContext::getActiveFrameIndex() const {
    return activeFrameIndex;
}

void RenderContext::submitAndPresent(CommandBuffer& commandBuffer, VkFence fence) {
    getSwapChainImage().getVkImage().transitionLayout(commandBuffer, VulkanLayout::PRESENT);
    commandBuffer.endRecord();

    auto queue = device.getQueueByFlag(VK_QUEUE_GRAPHICS_BIT, 0);

    VkSubmitInfo         submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore          waitSems[]   = {semaphores.presentFinishedSem};
    VkSemaphore          signalSems[] = {semaphores.renderFinishedSem};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores    = waitSems;
    submitInfo.pWaitDstStageMask  = waitStages;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSems;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = commandBuffer.getHandlePointer();

    queue.submit({submitInfo}, fence);

    if (swapchain) {
        VkSwapchainKHR vk_swapchain = swapchain->getHandle();

        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores    = &semaphores.renderFinishedSem;
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = &vk_swapchain;
        present_info.pImageIndices      = &activeFrameIndex;

        if (queue.present(present_info) == VK_ERROR_OUT_OF_DATE_KHR) {
            handleSurfaceChanges();
        }

        frameActive = false;
    }
    queue.wait();
}

void RenderContext::submit(CommandBuffer& commandBuffer, bool waiteFence, VkQueueFlagBits queueFlags) {
    commandBuffer.endRecord();
    auto queue = device.getQueueByFlag(queueFlags, 0);

    VkSubmitInfo         submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = commandBuffer.getHandlePointer();

    VkFence fence = VK_NULL_HANDLE;
    if (waiteFence) {
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkCreateFence(device.getHandle(), &fenceInfo, nullptr, &fence);
    }

    queue.submit({submitInfo}, fence);
    queue.wait();
    if (waiteFence) {
        vkWaitForFences(device.getHandle(), 1, &fence, VK_TRUE, UINT64_MAX);
    }
}

VkExtent2D RenderContext::getViewPortExtent() const {
    return virtualViewport->getExtent();
}

VkExtent3D RenderContext::getViewPortExtent3D() const {
    VkExtent3D extent{getViewPortExtent().width, getViewPortExtent().height, 1};
    return extent;
}

VkExtent2D RenderContext::getSwapChainExtent() const {
    return surfaceExtent;
}
uint32_t RenderContext::getSwapChainImageCount() const {
    return swapchain->getImageCount();
}

void RenderContext::setActiveFrameIdx(int idx) {
    activeFrameIndex = idx;
}

bool RenderContext::isPrepared() const {
    return prepared;
}

PipelineState& RenderContext::getPipelineState() {
    return pipelineState;
}

SgImage& RenderContext::getCurHwtexture() {
    return virtualViewport->getImage(activeFrameIndex);
}
SgImage& RenderContext::getSwapChainImage() {
    return hwTextures[activeFrameIndex];
}

// RenderContext & RenderContext::bindBuffer(uint32_t setId, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range,
//                                uint32_t binding, uint32_t array_element)
// {
//     resourceSets[setId].bindBuffer(buffer, offset, range, binding, array_element);
//     return *this;
// }

RenderContext& RenderContext::bindBuffer(uint32_t binding, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t setId, uint32_t array_element) {
    if (range == 0) range = buffer.getSize();
    if (setId == -1)
        setId = static_cast<uint32_t>(DescriptorSetPoints::UNIFORM);
    resourceSets[setId].bindBuffer(buffer, offset, range, binding, array_element);
    resourceSetsDirty = true;
    return *this;
}

RenderContext& RenderContext::bindAcceleration(uint32_t binding, const Accel& acceleration, uint32_t setId, uint32_t array_element) {
    if (setId == -1)
        setId = static_cast<uint32_t>(DescriptorSetPoints::ACCELERATION);
    resourceSets[setId].bindAccel(acceleration, binding, array_element);
    resourceSetsDirty = true;
    return *this;
}

RenderContext& RenderContext::bindImageSampler(uint32_t binding, const ImageView& view, const Sampler& sampler, uint32_t setId, uint32_t array_element) {
    if (setId == -1)
        setId = static_cast<uint32_t>(DescriptorSetPoints::SAMPLER);
    resourceSets[setId].bindImage(view, sampler, binding, array_element);
    resourceSetsDirty = true;
    return *this;
}

RenderContext& RenderContext::bindImage(uint32_t binding, const ImageView& view, uint32_t setId, uint32_t array_element) {
    if (setId == -1)
        setId = static_cast<uint32_t>(DescriptorSetPoints::INPUT);
    resourceSets[setId].bindInput(view, binding, array_element);
    resourceSetsDirty = true;
    return *this;
}
RenderContext& RenderContext::bindSampler(uint32_t binding, const Sampler& sampler, uint32_t setId, uint32_t array_element) {
    if (setId == -1)
        setId = static_cast<uint32_t>(DescriptorSetPoints::SAMPLER);
    resourceSets[setId].bindSampler(sampler, binding, array_element);
    resourceSetsDirty = true;
    return *this;
}

// RenderContext& RenderContext::bindMaterial(const Material& material) {
//     const uint32_t setId = static_cast<uint32_t>(DescriptorSetPoints::SAMPLER);
//     if (!pipelineState.getPipelineLayout().hasLayout(setId))
//         return *this;
//     auto& descriptorLayout = pipelineState.getPipelineLayout().getDescriptorLayout(setId);
//
//     for (auto& texture : material.textures) {
//         if (descriptorLayout.hasLayoutBinding(texture.first)) {
//             auto& binding = descriptorLayout.getLayoutBindingInfo(texture.first);
//             bindImageSampler(binding.binding, texture.second.image->getVkImageView(), texture.second.getSampler(), setId, 0);
//         }
//     }
//     return *this;
// }
RenderContext& RenderContext::bindView(const View& view) {
    //materials and textures
    // auto             materials  = view.GetMMaterials();
    // BufferAllocation allocation = allocateBuffer(sizeof(GltfMaterial) * materials.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    // allocation.buffer->uploadData(materials.data(), allocation.size, allocation.offset);
    // bindBuffer(2, *allocation.buffer, allocation.offset);
    //
    // const auto& textures = view.GetMTextures();
    // for (uint32_t i = 0; i < textures.size(); i++) {
    //     bindImageSampler(0, textures[i]->getImage().getVkImageView(), textures[i]->getSampler(), 1, i);
    // }
    return *this;
}

RenderContext& RenderContext::bindPrimitiveGeom(CommandBuffer& commandBuffer, const Primitive& primitive) {
    VertexInputState vertexInputState;
    uint32_t         maxLoaction = 0;

    for (const auto& inputResource : pipelineState.getPipelineLayout().getShaderResources(ShaderResourceType::Input,
                                                                                          VK_SHADER_STAGE_VERTEX_BIT)) {
        auto inputResourceName = inputResource.name;
        //resource name in shader is in_XXX,so we need to remove the prefix
        auto splitPos = inputResourceName.find("in_");
        if (splitPos != std::string::npos) {
            inputResourceName = inputResourceName.substr(splitPos + 3);
        }

        VertexAttribute attribute{};
        if (!primitive.getVertexAttribute(inputResourceName, &attribute)) {
            if (inputResourceName != "primitive_id")
                LOGW("Primitive does not have vertex buffer for input resource {}", inputResourceName);
            continue;
        }
        VkVertexInputAttributeDescription vertex_attribute{};
        vertex_attribute.binding  = inputResource.location;
        vertex_attribute.format   = attribute.format;
        vertex_attribute.location = inputResource.location;
        vertex_attribute.offset   = attribute.offset;

        vertexInputState.attributes.push_back(vertex_attribute);

        VkVertexInputBindingDescription vertex_binding{};
        vertex_binding.binding = inputResource.location;
        vertex_binding.stride  = attribute.stride;

        vertexInputState.bindings.push_back(vertex_binding);

        if (primitive.hasVertexBuffer(inputResourceName)) {
            std::vector<const Buffer*> buffers = {&primitive.getVertexBuffer(inputResourceName)};
            commandBuffer.bindVertexBuffer(inputResource.location, buffers, {0});
            maxLoaction = std::max(maxLoaction, inputResource.location);
        }
    }
    if (primitive.hasIndexBuffer())
        commandBuffer.bindIndicesBuffer(primitive.getIndexBuffer(), 0, primitive.getIndexType());
    InputAssemblyState inputAssemblyState = pipelineState.getInputAssemblyState();
    inputAssemblyState.topology           = GetVkPrimitiveTopology(primitive.primitiveType);

    // vertexInputState.attributes.push_back({.location = maxLoaction + 1, .binding = maxLoaction + 1, .format = VK_FORMAT_R32_UINT, .offset = 0});
    //  vertexInputState.bindings.push_back({.binding = maxLoaction + 1, .stride = sizeof(uint32_t), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE});

    pipelineState.setVertexInputState(vertexInputState).setInputAssemblyState(inputAssemblyState);

    //  bindPushConstants(primitive);
    // bindBuffer(static_cast<uint32_t>(UniformBindingPoints::PRIM_INFO), primitive.getUniformBuffer(), 0, sizeof(PerPrimitiveUniform));
    return *this;
    // return bindMaterial(primitive.material);
}

RenderContext& RenderContext::bindScene(CommandBuffer& commandBuffer, const Scene& scene) {
    VertexInputState vertexInputState;
    uint32_t         maxLoaction = 0;
    for (const auto& inputResource : pipelineState.getPipelineLayout().getShaderResources(ShaderResourceType::Input,
                                                                                          VK_SHADER_STAGE_VERTEX_BIT)) {
        auto inputResourceName = inputResource.name;
        //resource name in shader is in_XXX,so we need to remove the prefix
        auto splitPos = inputResourceName.find("in_");
        if (splitPos != std::string::npos) {
            inputResourceName = inputResourceName.substr(splitPos + 3);
        }

        VertexAttribute attribute{};
        if (!scene.getVertexAttribute(inputResourceName, &attribute)) {
            if (inputResourceName != "primitive_id")
                LOGW("Primitive does not have vertex buffer for input resource {}", inputResourceName);
            continue;
        }
        VkVertexInputAttributeDescription vertex_attribute{};
        vertex_attribute.binding  = inputResource.location;
        vertex_attribute.format   = attribute.format;
        vertex_attribute.location = inputResource.location;
        vertex_attribute.offset   = attribute.offset;

        vertexInputState.attributes.push_back(vertex_attribute);

        VkVertexInputBindingDescription vertex_binding{};
        vertex_binding.binding = inputResource.location;
        vertex_binding.stride  = attribute.stride;

        vertexInputState.bindings.push_back(vertex_binding);

        if (scene.hasVertexBuffer(inputResourceName)) {
            std::vector<const Buffer*> buffers = {&scene.getVertexBuffer(inputResourceName)};
            commandBuffer.bindVertexBuffer(inputResource.location, buffers, {0});
            maxLoaction = std::max(maxLoaction, inputResource.location);
        }
    }

    if (scene.usePrimitiveIdBuffer()) {
        vertexInputState.attributes.push_back({.location = maxLoaction + 1, .binding = maxLoaction + 1, .format = VK_FORMAT_R32_UINT, .offset = 0});
        vertexInputState.bindings.push_back({.binding = maxLoaction + 1, .stride = sizeof(uint32_t), .inputRate = scene.getMergeDrawCall() ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE});
        std::vector<const Buffer*> buffers = {&scene.getPrimitiveIdBuffer()};
        commandBuffer.bindVertexBuffer(maxLoaction + 1, buffers, {0});
    }

    commandBuffer.bindIndicesBuffer(scene.getIndexBuffer(), 0, scene.getIndexType());

    pipelineState.setVertexInputState(vertexInputState);

    bindBuffer(static_cast<uint32_t>(UniformBindingPoints::PRIM_INFO), scene.getUniformBuffer(), 0, scene.getUniformBuffer().getSize());
    return *this;
}
RenderContext& RenderContext::bindPrimitiveShading(CommandBuffer& commandBuffer, const Primitive& primitive) {
    bindPushConstants(primitive.materialIndex);
    return *this;
}

const std::unordered_map<uint32_t, ResourceSet>& RenderContext::getResourceSets() const {
    return resourceSets;
}

VkPipelineBindPoint RenderContext::getPipelineBindPoint() const {
    switch (pipelineState.getPipelineType()) {
        case PIPELINE_TYPE::E_GRAPHICS:
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case PIPELINE_TYPE::E_COMPUTE:
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        case PIPELINE_TYPE::E_RAY_TRACING:
            return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        default:
            LOGE("pipeline bind point not support");
    }
    return VK_PIPELINE_BIND_POINT_GRAPHICS;
}

void RenderContext::flushDescriptorState(CommandBuffer& commandBuffer, VkPipelineBindPoint pipeline_bind_point) {
    auto& pipelineLayout = pipelineState.getPipelineLayout();

    std::unordered_set<uint32_t> update_descriptor_sets;

    for (auto& set_it : pipelineLayout.getShaderSets()) {
        uint32_t descriptor_set_id = set_it.first;

        auto descriptor_set_layout_it = descriptor_set_layout_binding_state.find(descriptor_set_id);

        if (descriptor_set_layout_it != descriptor_set_layout_binding_state.end()) {
            if (descriptor_set_layout_it->second->getHandle() != pipelineLayout.getDescriptorLayout(descriptor_set_id).getHandle()) {
                update_descriptor_sets.emplace(descriptor_set_id);
            }
        }
    }

    for (auto set_it = descriptor_set_layout_binding_state.begin(); set_it != descriptor_set_layout_binding_state.end();) {
        if (!pipelineLayout.hasLayout(set_it->first)) {
            set_it = descriptor_set_layout_binding_state.erase(set_it);
        } else {
            ++set_it;
        }
    }

    //Two case wee need to update descriptor sets
    //1. descriptor set layout has been changed
    //2. resourceSets is dirty
    if (resourceSetsDirty || !update_descriptor_sets.empty()) {

        resourceSetsDirty = false;

        for (auto& resourceSetIt : resourceSets) {

            //setId -> pair(binding,arrayElement)
            BindingMap<VkDescriptorBufferInfo>                       bufferInfos;
            BindingMap<VkDescriptorImageInfo>                        imageInfos;
            BindingMap<VkWriteDescriptorSetAccelerationStructureKHR> accelerationInfos;

            std::vector<uint32_t> dynamic_offsets;

            auto  descriptorSetID = resourceSetIt.first;
            auto& resourceSet     = resourceSetIt.second;

            if (!resourceSet.isDirty() && !update_descriptor_sets.contains(descriptorSetID))
                continue;

            resourceSet.clearDirty();

            if (!pipelineState.getPipelineLayout().hasLayout(descriptorSetID))
                continue;

            auto& descriptorSetLayout                            = pipelineLayout.getDescriptorLayout(descriptorSetID);
            descriptor_set_layout_binding_state[descriptorSetID] = &descriptorSetLayout;

            for (auto& bindingIt : resourceSet.getResourceBindings()) {
                auto  bindingIndex     = bindingIt.first;
                auto& bindingResources = bindingIt.second;

                for (auto& elementIt : bindingResources) {
                    auto  arrayElement = elementIt.first;
                    auto& resourceInfo = elementIt.second;

                    auto& buffer    = resourceInfo.buffer;
                    auto& sampler   = resourceInfo.sampler;
                    auto& imageView = resourceInfo.image_view;
                    auto& accel     = resourceInfo.accel;

                    if (!descriptorSetLayout.hasLayoutBinding(bindingIndex) || descriptorSetLayout.getLayoutBindingInfo(bindingIndex).descriptorCount <= arrayElement)
                        continue;
                    auto& bindingInfo = descriptorSetLayout.getLayoutBindingInfo(bindingIndex);

                    if (buffer != nullptr) {
                        VkDescriptorBufferInfo bufferInfo{
                            .buffer = buffer->getHandle(), .offset = resourceInfo.offset, .range = resourceInfo.range};

                        bufferInfos[bindingIndex][arrayElement] = bufferInfo;
                    }

                    if (imageView != nullptr || sampler != nullptr) {
                        VkDescriptorImageInfo imageInfo{
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        };

                        if(imageView) {
                            imageInfo.imageView = imageView->getHandle();
                        }
                        if (sampler)
                            imageInfo.sampler = sampler->getHandle();

                        if (imageView != nullptr) {
                            // Add image layout info based on descriptor type
                            switch (bindingInfo.descriptorType) {
                                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    break;
                                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                                    if (isDepthOrStencilFormat(imageView->getFormat())) {
                                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                                    } else {
                                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    }
                                    break;
                                case VK_DESCRIPTOR_TYPE_SAMPLER:
                                   // imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    break;
                                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                                    break;
                                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    break;
                                default:
                                    continue;
                            }
                        }
                        imageInfos[bindingIndex][arrayElement] = imageInfo;
                    }

                    if (accel != nullptr) {
                        VkWriteDescriptorSetAccelerationStructureKHR accelInfo{
                            .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                            .accelerationStructureCount = 1,
                            .pAccelerationStructures    = &accel->accel};
                        accelerationInfos[bindingIndex][arrayElement] = accelInfo;
                    }
                }
            }
            auto& descriptorPool = device.getResourceCache().requestDescriptorPool(descriptorSetLayout);
            auto& descriptorSet  = device.getResourceCache().requestDescriptorSet(
                descriptorSetLayout, descriptorPool, bufferInfos, imageInfos, accelerationInfos);
            descriptorSet.update({});
            commandBuffer.bindDescriptorSets(pipeline_bind_point, pipelineLayout.getHandle(), descriptorSetID, {&descriptorSet}, dynamic_offsets);
        }
    }
}

void RenderContext::flushPipelineState(CommandBuffer& commandBuffer) {
    if (pipelineState.isDirty()) {
        commandBuffer.bindPipeline(device.getResourceCache().requestPipeline(this->getPipelineState()),
                                   getPipelineBindPoint());
        pipelineState.clearDirty();
    }
}

void RenderContext::flushAndDrawIndexed(CommandBuffer& commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) {
    flush(commandBuffer);
    commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void RenderContext::flushAndDraw(CommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    flush(commandBuffer);
    commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderContext::traceRay(CommandBuffer& commandBuffer, VkExtent3D dims) {
    CHECK_RESULT((getPipelineState().getPipelineType() == PIPELINE_TYPE::E_RAY_TRACING));
    if (dims.width == 0 || dims.height == 0 || dims.depth == 0) {
        dims = {getViewPortExtent().width, getViewPortExtent().height, 1};
    }
    flush(commandBuffer);
    auto& pipeline           = device.getResourceCache().requestPipeline(this->getPipelineState());
    auto& sbtWarpper         = pipeline.getSbtWarpper();
    auto& shaderBindingTable = sbtWarpper.getRegions();
    vkCmdTraceRaysKHR(commandBuffer.getHandle(), &shaderBindingTable[0], &shaderBindingTable[1], &shaderBindingTable[2], &shaderBindingTable[3], dims.width, dims.height, dims.depth);
}

void RenderContext::flushAndDispatch(CommandBuffer& commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    flush(commandBuffer);
    vkCmdDispatch(commandBuffer.getHandle(), groupCountX, groupCountY, groupCountZ);
}

void RenderContext::flushAndDispatchIndirect(CommandBuffer& commandBuffer, Buffer& buffer, VkDeviceSize offset) {
    flush(commandBuffer);
    vkCmdDispatchIndirect(commandBuffer.getHandle(), buffer.getHandle(), offset);
}

void RenderContext::flushAndDrawMeshTasks(CommandBuffer& commandBuffer, uint groupCountX, uint groupCountY, uint groupCountZ) {
    flush(commandBuffer);
    vkCmdDrawMeshTasksEXT(commandBuffer.getHandle(), groupCountX, groupCountY, groupCountZ);
}

void RenderContext::flushAndDrawMeshTasksIndirect(CommandBuffer& commandBuffer, Buffer& buffer, VkDeviceSize offset) {
    flush(commandBuffer);
    vkCmdDrawMeshTasksIndirectNV(commandBuffer.getHandle(), buffer.getHandle(), offset, 1, 0);
}

void RenderContext::beginRenderPass(CommandBuffer& commandBuffer, RenderTarget& renderTarget, const std::vector<SubpassInfo>& subpassInfos) {
    auto& renderPass  = device.getResourceCache().requestRenderPass(renderTarget.getAttachments(), subpassInfos);
    auto& framebuffer = device.getResourceCache().requestFrameBuffer(
        renderTarget, renderPass, renderTarget.getExtent());

    commandBuffer.beginRenderPass(renderPass, framebuffer, renderTarget.getDefaultClearValues(), {});

    ColorBlendState colorBlendState = pipelineState.getColorBlendState();
    colorBlendState.attachments.resize(renderPass.getColorOutputCount(0));
    pipelineState.setColorBlendState(colorBlendState);

    pipelineState.setRenderPass(renderPass);
    pipelineState.setSubpassIndex(0);
}

void RenderContext::nextSubpass(CommandBuffer& commandBuffer) {
    // clearPassResources();
    //   resourceSets.clear();
    storePushConstants.clear();

    uint32_t subpassIdx = pipelineState.getSubpassIndex();
    pipelineState.setSubpassIndex(subpassIdx + 1);

    ColorBlendState colorBlendState = pipelineState.getColorBlendState();
    colorBlendState.attachments.resize(pipelineState.getRenderPass()->getColorOutputCount(subpassIdx + 1));
    pipelineState.setColorBlendState(colorBlendState);

    vkCmdNextSubpass(commandBuffer.getHandle(), {});
}

void RenderContext::flushPushConstantStage(CommandBuffer& commandBuffer) {
    // commandBuffer
    if (storePushConstants.empty())
        return;

    auto& pipelineLayout    = pipelineState.getPipelineLayout();
    auto  pushConstantRange = pipelineLayout.getPushConstantRangeStage(storePushConstants.size());

    if (pushConstantRange == 0) {
        pushConstantRange = pipelineLayout.getPushConstantRangeStage(storePushConstants.size());
        LOGE("Push Constant Size is too large,device support {},but current size is {}", maxPushConstantSize, storePushConstants.size());
    }
    vkCmdPushConstants(commandBuffer.getHandle(), pipelineLayout.getHandle(), pushConstantRange, 0, storePushConstants.size(), storePushConstants.data());
    storePushConstants.clear();
}

void RenderContext::flush(CommandBuffer& commandBuffer) {
    flushPipelineState(commandBuffer);
    flushDescriptorState(commandBuffer, getPipelineBindPoint());
    flushPushConstantStage(commandBuffer);
}

void RenderContext::endRenderPass(CommandBuffer& commandBuffer, RenderTarget& renderTarget) {
    commandBuffer.endPass();
    // resourceSets.clear();

    // auto& finalLayouts = pipelineState.getRenderPass()->getAttachmentFinalLayouts();
    // for (uint32_t i = 0; i < finalLayouts.size(); i++) {
    //     renderTarget.getImage(i).setLayout(finalLayouts[i]);
    // }

    pipelineState.reset();
}
void RenderContext::resetViewport(CommandBuffer& commandBuffer) {
    commandBuffer.setViewport(0, {vkCommon::initializers::viewport(float(getViewPortExtent().width), float(getViewPortExtent().height), 0.0f, 1.0f, flipViewport)});
    commandBuffer.setScissor(0, {vkCommon::initializers::rect2D(float(getViewPortExtent().width), float(getViewPortExtent().height), 0, 0)});
}

void RenderContext::clearPassResources() {
    resourceSets.clear();
    storePushConstants.clear();
    pipelineState.reset();
}
template<>
RenderContext& RenderContext::bindPushConstants(const std::vector<uint8_t>& pushConstants) {
    auto size = pushConstants.size() + storePushConstants.size();
    if (size > maxPushConstantSize) {
        LOGE("Push Constant Size is too large,device support {},but current size is {}", maxPushConstantSize, size);
    }
    storePushConstants.insert(storePushConstants.end(), pushConstants.begin(), pushConstants.end());
    return *this;
}
RenderContext& RenderContext::bindShaders(const ShaderPipelineKey& shaderKeys) {
    auto& pipelineLayout = device.getResourceCache().requestPipelineLayout(shaderKeys);
    pipelineState.setPipelineLayout(pipelineLayout);
    return *this;
}

BufferAllocation RenderContext::allocateBuffer(VkDeviceSize allocateSize, VkBufferUsageFlags usage) {
    auto& frameResource = frameResources[activeFrameIndex];
    // assert(frameResource.bufferPools.contains(usage), "Buffer usage not contained");
    return frameResource->bufferPools.at(usage)->AllocateBufferBlock(allocateSize);
}

CommandBuffer& RenderContext::getGraphicCommandBuffer() {
    return *frameResources[activeFrameIndex]->graphicCommandBuffer;
}

CommandBuffer& RenderContext::getComputeCommandBuffer() {
    return *frameResources[activeFrameIndex]->graphicCommandBuffer;
}

Device& RenderContext::getDevice() {
    return device;
}

void RenderContext::handleSurfaceChanges() {
    VkSurfaceCapabilitiesKHR surface_properties;
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.getPhysicalDevice(),
                                                              swapchain->getSurface(),
                                                              &surface_properties))

    // if (surfaceExtent.width != surface_properties.currentExtent.width || surfaceExtent.height != surface_properties.currentExtent.height) {
    device.waitIdle();
    surfaceExtent = surface_properties.currentExtent;
    recrateSwapChain(surfaceExtent);
    //  }
}

void RenderContext::recrateSwapChain(VkExtent2D extent) {
    device.getResourceCache().clearFrameBuffers();
    device.getResourceCache().clearSgImages();

    hwTextures.clear();

    auto surface = swapchain->getSurface();
    swapchain    = nullptr;
    swapchain    = std::make_unique<SwapChain>(device, surface, extent);

    if (swapchain) {
        surfaceExtent = swapchain->getExtent();

        VkExtent3D extent3d{surfaceExtent.width, surfaceExtent.height, 1};

        for (auto& image_handle : swapchain->getImages()) {
            auto swapChainImage = Image(device, image_handle, extent3d, swapchain->getImageFormat(), swapchain->getUseage());
            hwTextures.emplace_back(device, image_handle, extent3d, swapchain->getImageFormat(), swapchain->getUseage());
        }
    }
}

void RenderContext::copyBuffer(const Buffer& src, Buffer& dst) {
    auto         commandBuffer = device.createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkBufferCopy copy_region   = {};
    copy_region.size           = src.getSize();
    vkCmdCopyBuffer(commandBuffer.getHandle(), src.getHandle(), dst.getHandle(), 1, &copy_region);
    submit(commandBuffer);
}
void RenderContext::setFlipViewport(bool flip) {
    flipViewport = flip;
}
bool RenderContext::getFlipViewport() const {
    return flipViewport;
}
