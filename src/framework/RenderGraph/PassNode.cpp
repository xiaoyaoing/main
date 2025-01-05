#include "PassNode.h"

#include "Core/Device/Device.h"
#include <Common/ResourceCache.h>

#include "Core/RenderContext.h"
#include "RenderGraph/RenderGraph.h"
#include "ext/magick_enum/include/magic_enum/magic_enum.hpp"
#include <format>

#include <set>
#include <unordered_set>

std::string getPassName(const std::string& passType, const std::string& passName) {
    return std::string(passType) + " " + passName;
}

void GraphicsPassNode::RenderPassData::devirtualize(RenderGraph& renderGraph, const GraphicsPassNode& node) {
    std::vector<SgImage*>   images;
    std::vector<Attachment> attachments;

    std::unordered_set<RenderGraphHandle, RenderGraphHandle::Hash> attachment_textures_set;
    for (auto& subpass : desc.subpasses) {
        for (auto& handle : subpass.inputAttachments) {
            // if(!attachment_textures.contains(handle))
            //     attachment_textures[handle] = index++;
            attachment_textures_set.insert(handle);
        }
        for (auto& handle : subpass.outputAttachments) {
            // if(!attachment_textures.contains(handle))
            //     attachment_textures[handle] = index++;
            attachment_textures_set.insert(handle);
        }
    }

    uint32_t index = 0;
    for (const auto& color : desc.textures) {
        if (!attachment_textures_set.contains(color))
            continue;

        auto texture   = renderGraph.getTexture(color);
        auto hwTexture = texture->getHwTexture();

        auto write = renderGraph.isWrite(color, &node);
        auto read  = renderGraph.isRead(color, &node);

        auto loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        if (write && !read) {
            loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        auto attachment = Attachment{
            .format         = hwTexture->getFormat(),
            .samples        = hwTexture->getVkImage().getSampleCount(),
            .usage          = hwTexture->getVkImage().getUseFlags(),
            .initial_layout = hwTexture->getVkImage().getLayout(hwTexture->getVkImageView().getSubResourceRange()),
            .final_layout =  desc.finalLayouts.contains(color)?desc.finalLayouts.at(color):VulkanLayout::UNDEFINED,
            //todo fix this
            .loadOp = loadOp,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE
        };

        images.push_back(hwTexture);
        attachments.push_back(attachment);
        attachment_textures[color] = index++;
    }

    VkExtent2D extent = (desc.extent2D.width == 0 && desc.extent2D.height == 0) ? g_context->getViewPortExtent() : desc.extent2D;
    renderTarget      = std::make_unique<RenderTarget>(images, attachments, extent);
}

RenderTarget& GraphicsPassNode::RenderPassData::getRenderTarget() {
    return *renderTarget;
}

VkPipeline pipeline = VK_NULL_HANDLE;

void GraphicsPassNode::execute(RenderGraph& renderGraph, CommandBuffer& commandBuffer) {
    renderPassData.devirtualize(renderGraph, *this);

    auto& renderTarget = renderPassData.getRenderTarget();

    std::vector<SubpassInfo> subpassInfos;

    {
        auto& renderGraphSubpassInfos = renderPassData.desc.subpasses;

        std::unordered_map<RenderGraphHandle, size_t, RenderGraphHandle::Hash> attachmentMap = renderPassData.getAttachmentTextures();

        uint32_t index = 0;

        std::ranges::transform(renderGraphSubpassInfos.begin(), renderGraphSubpassInfos.end(), std::back_inserter(subpassInfos), [&](const auto& renderGraphSubassInfo) {
            SubpassInfo subpassInfo{};
            subpassInfo.disableDepthStencilAttachment = renderGraphSubassInfo.disableDepthTest;
            subpassInfo.inputAttachments.resize(renderGraphSubassInfo.inputAttachments.size());
            subpassInfo.outputAttachments.resize(renderGraphSubassInfo.outputAttachments.size());
            std::ranges::transform(renderGraphSubassInfo.inputAttachments.begin(),
                                   renderGraphSubassInfo.inputAttachments.end(),
                                   subpassInfo.inputAttachments.begin(),
                                   [&](const auto& handle) {
                                       return attachmentMap[handle];
                                   });
            std::ranges::transform(renderGraphSubassInfo.outputAttachments.begin(),
                                   renderGraphSubassInfo.outputAttachments.end(),
                                   subpassInfo.outputAttachments.begin(),
                                   [&](const auto& handle) {
                                       return attachmentMap[handle];
                                   });
            return subpassInfo;
        });
    }

    auto hwTextures = renderTarget.getHwTextures();
    g_context->getPipelineState().setPipelineType(PIPELINE_TYPE::E_GRAPHICS);
    g_context->beginRenderPass(commandBuffer, renderTarget, subpassInfos);

    RenderPassContext context = {
        .commandBuffer = commandBuffer, .renderGraph = renderGraph};

    auto passName = getPassName("Render Pass", getName());
    DebugUtils::CmdBeginLabel(context.commandBuffer.getHandle(), passName, {1, 0, 0, 1});
    mRenderPass->execute(context);
    DebugUtils::CmdEndLabel(context.commandBuffer.getHandle());

    g_context->endRenderPass(commandBuffer, renderTarget);
    g_context->clearPassResources();
}

void GraphicsPassNode::declareRenderTarget(const std::string& name, const RenderGraphPassDescriptor& descriptor) {
    renderPassData.name = name;

    renderPassData.desc = descriptor;
}

GraphicsPassNode::GraphicsPassNode(RenderGraph& renderGraph, const std::string& name, RenderGraphPassBase* base) : PassNode(name), mRenderPass(
                                                                                                                                       base),
                                                                                                                   name(name) {
}

void GraphicsPassNode::declareRenderPass(const RenderGraphPassDescriptor& descriptor) {
    renderPassData.desc = descriptor;
}

PassNode::PassNode(const std::string& passName) {
    setName(passName);
}

std::string usageToString(uint16_t usage,RenderResourceType type) {
    if (type == RenderResourceType::ETexture) {
        return std::string(magic_enum::enum_name(static_cast<TextureUsage>(usage)));

	} else {
		return std::string(magic_enum::enum_name(static_cast<BufferUsage>(usage)));
	}
    return "UNDEFINED";
}

void PassNode::resolveResourceUsages(RenderGraph& renderGraph, CommandBuffer& commandBuffer) {
    ResourceBarrierInfo barrierInfo;
    for (auto& resourceIt : mResourceUsage) {
        auto  resource     = renderGraph.getResource(resourceIt.first);
        auto& stateTracker = renderGraph.getResourceStateTracker();

        auto state = stateTracker.getResourceState(resourceIt.first);

        unsigned short srcUsage;
        RenderPassType srcState{UNDEFINED};

        if (std::get<0>(state) == nullptr) {
            //srcState = getType();
            srcUsage = resource->getDefaultUsage(resourceIt.second);
        } else {
            srcState = std::get<0>(state)->getType();
            srcUsage = std::get<1>(state);
        }
        stateTracker.setResourceState(resourceIt.first, this, resourceIt.second);
        resource->resloveUsage(barrierInfo, srcUsage, resourceIt.second, srcState, getType());

        if (renderGraph.getDebugBarrierInfo()) {
            LOGI("Resource {} from {} to {} before pass{}", resource->getName(), usageToString(srcUsage,resource->getType()), usageToString(resourceIt.second,resource->getType()), getName());
        }
    }
    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = barrierInfo.bufferBarriers.size();
    dependencyInfo.imageMemoryBarrierCount  = barrierInfo.imageBarriers.size();
    dependencyInfo.memoryBarrierCount       = barrierInfo.memoryBarriers.size();
    dependencyInfo.pBufferMemoryBarriers    = barrierInfo.bufferBarriers.data();
    dependencyInfo.pImageMemoryBarriers     = barrierInfo.imageBarriers.data();
    dependencyInfo.pMemoryBarriers          = barrierInfo.memoryBarriers.data();

    vkCmdPipelineBarrier2(commandBuffer.getHandle(), &dependencyInfo);
}

void PassNode::addResourceUsage(RenderGraphHandle handle, uint16_t usage) {
    if (mResourceUsage.contains(handle))
        mResourceUsage[handle] |= usage;
    else
        mResourceUsage.emplace(handle, usage);
}
uint16_t PassNode::getResourceUsage(RenderGraphHandle handle) const {
    if (mResourceUsage.contains(handle))
        return mResourceUsage.at(handle);
    return 0;
}

void ImageCopyPassNode::execute(RenderGraph& renderGraph, CommandBuffer& commandBuffer) {

    auto& srcVkImage = renderGraph.getTexture(src)->getHwTexture()->getVkImage();
    auto& dstVkImage = renderGraph.getTexture(dst)->getHwTexture()->getVkImage();

    VkImageBlit2 blitImageRegion{};
    blitImageRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitImageRegion.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;

    VkOffset3D dstOffset = {static_cast<int32_t>(dstVkImage.getExtent().width), static_cast<int32_t>(dstVkImage.getExtent().height), 1};
    VkOffset3D srcOffset = {static_cast<int32_t>(srcVkImage.getExtent().width), static_cast<int32_t>(srcVkImage.getExtent().height), 1};

    blitImageRegion.srcOffsets[0]  = {0, 0, 0};
    blitImageRegion.srcOffsets[1]  = srcOffset;
    blitImageRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitImageRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitImageRegion.dstOffsets[0]  = {0, 0, 0};
    blitImageRegion.dstOffsets[1]  = dstOffset;

    VkBlitImageInfo2 blitImageInfo{};
    blitImageInfo.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitImageInfo.srcImage       = srcVkImage.getHandle();
    blitImageInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitImageInfo.dstImage       = dstVkImage.getHandle();
    blitImageInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitImageInfo.regionCount    = 1;
    blitImageInfo.pRegions       = &blitImageRegion;

    vkCmdBlitImage2(commandBuffer.getHandle(), &blitImageInfo);
}

ImageCopyPassNode::ImageCopyPassNode(RenderGraphHandle src, RenderGraphHandle dst) : PassNode("Image Copy"), src(src), dst(dst) {
}
RenderPassType ImageCopyPassNode::getType() const {
    return RenderPassType::TRANSFER;
}

void ComputePassNode::execute(RenderGraph& renderGraph, CommandBuffer& commandBuffer) {
    g_context->getPipelineState().setPipelineType(PIPELINE_TYPE::E_COMPUTE);
    if (mPass->getData().pipelineLayout)
        g_context->getPipelineState().setPipelineLayout(*mPass->getData().pipelineLayout);

    RenderPassContext context{.commandBuffer = commandBuffer, .renderGraph = renderGraph};

    auto passName = getPassName("Compute Pass", getName());
    DebugUtils::CmdBeginLabel(commandBuffer.getHandle(), passName, {1, 0, 0, 1});
    mPass->execute(context);
    DebugUtils::CmdEndLabel(context.commandBuffer.getHandle());

    g_context->clearPassResources();
}

void RayTracingPassNode::execute(RenderGraph& renderGraph, CommandBuffer& commandBuffer) {
    auto& settings = mPass->getData();
    g_context->getPipelineState().setPipelineType(PIPELINE_TYPE::E_RAY_TRACING).setrTPipelineSettings(settings.rTPipelineSettings);
    if (settings.pipelineLayout)
        g_context->getPipelineState().setPipelineLayout(*settings.pipelineLayout);
    else {
        CHECK_RESULT(settings.shaderPaths.size() > 0, "No shader paths provided for ray tracing pass");
        g_context->bindShaders(settings.shaderPaths);
    }
    g_context->bindAcceleration(0, *settings.accel);
    RenderPassContext context{.commandBuffer = commandBuffer, .renderGraph = renderGraph};

    auto passName = getPassName("RayTracing Pass", getName());
    DebugUtils::CmdBeginLabel(context.commandBuffer.getHandle(), passName, {1, 0, 0, 1});
    mPass->execute(context);
    DebugUtils::CmdEndLabel(context.commandBuffer.getHandle());

    g_context->clearPassResources();
    // g_context->getPipelineState().setPipelineType(PIPELINE_TYPE::E_RAY_TRACING).setPipelineLayout(*layout).setrTPipelineSettings({.maxDepth = 5,.dims = {width,height,1}});
    // renderContext->getPipelineState().setPipelineType(PIPELINE_TYPE::E_RAY_TRACING).setPipelineLayout(*layout).setrTPipelineSettings({.maxDepth = 5,.dims = {width,height,1}});
}

ComputePassNode::ComputePassNode(RenderGraph& renderGraph, const std::string& name, ComputeRenderGraphPass* base) : PassNode(name), mPass(base) {
}

RayTracingPassNode::RayTracingPassNode(RenderGraph& renderGraph, const std::string& name, RaytracingRenderGraphPass* base) : PassNode(name), mPass(base) {
}

RenderPassType GraphicsPassNode::getType() const {
    return RenderPassType::GRAPHICS;
}
RenderPassType ComputePassNode::getType() const {
    return RenderPassType::COMPUTE;
}
RenderPassType RayTracingPassNode::getType() const {
    return RenderPassType::RAYTRACING;
}
