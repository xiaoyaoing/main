#include "HizPass.h"

#include "Common/ResourceCache.h"
#include "RenderGraph/RenderGraph.h"

struct PushConstant {
    glm::vec4 params;
};

static std::vector<VkImageMemoryBarrier2> BuildHizBarrier(uint32_t level, Image& hiz) {

    if (level == 0) {
        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.resize(1);
        VkImageMemoryBarrier2& barrier = barriers[0];
        barrier.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.image                  = hiz.getHandle();
        barrier.oldLayout              = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout              = VK_IMAGE_LAYOUT_GENERAL;
        barrier.subresourceRange       = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
        barrier.srcAccessMask          = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask          = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcStageMask           = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.dstStageMask           = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        return barriers;
    } else {
        std::vector<VkImageMemoryBarrier2> barriers;
        barriers.resize(2);

        VkImageMemoryBarrier2& barrier = barriers[0];
        barrier.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.image                  = hiz.getHandle();
        barrier.oldLayout              = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout              = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.subresourceRange       = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1};
        barrier.srcAccessMask          = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask          = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcStageMask           = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.dstStageMask           = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;

        auto& barrier2               = barriers[1];
        barrier2.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier2.image               = hiz.getHandle();
        barrier2.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier2.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier2.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
        barrier2.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier2.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier2.srcStageMask        = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier2.dstStageMask        = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        return barriers;
    }
}

void HizPass::render(RenderGraph& rg) {
    rg.addComputePass(
        "Hiz Pass",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            auto depth           = rg.getBlackBoard().getHandle(DEPTH_IMAGE_NAME);
            auto depth_hierrachy = rg.getBlackBoard().getHandle("depth_hiz");
            //Layout will be handle explicitly
            //Write to the hiz texture but in execute we will transition it to read only
            //So here we use sampleable usage for the hiz texture
            builder.readTexture(depth, TextureUsage::SAMPLEABLE).writeTexture(depth_hierrachy, TextureUsage::SAMPLEABLE);
            settings.pipelineLayout = &rg.getDevice().getResourceCache().requestPipelineLayout(ShaderPipelineKey{"common/hiz.comp"});
        },
        [&](RenderPassContext& context) {
            auto&      hierrachy = rg.getBlackBoard().getHwImage("depth_hiz");
            auto       mip       = hierrachy.getMipLevelCount();
            VkExtent2D extent    = hierrachy.getExtent2D();
            for (uint32_t i = 0; i < mip; i++) {
                {
                    auto             barriers = BuildHizBarrier(i, hierrachy.getVkImage());
                   VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                   dependencyInfo.imageMemoryBarrierCount = barriers.size();
                   dependencyInfo.pImageMemoryBarriers    = barriers.data();
                   vkCmdPipelineBarrier2(context.commandBuffer.getHandle(), &dependencyInfo);
                }
                if (i == 0) {
                    g_context->bindImageSampler(0, rg.getBlackBoard().getImageView(DEPTH_IMAGE_NAME), rg.getDevice().getResourceCache().requestSampler());
                    g_context->bindImage(0, hierrachy.getVkImageView(VK_IMAGE_VIEW_TYPE_MAX_ENUM, VK_FORMAT_UNDEFINED, 0, 0, 1));

                } else {
                    g_context->bindImageSampler(0, hierrachy.getVkImageView(VK_IMAGE_VIEW_TYPE_MAX_ENUM, VK_FORMAT_UNDEFINED, i - 1, 0, 1), rg.getDevice().getResourceCache().requestSampler());
                    g_context->bindImage(0, hierrachy.getVkImageView(VK_IMAGE_VIEW_TYPE_MAX_ENUM, VK_FORMAT_UNDEFINED, i, 0, 1));
                }
                ivec2        dispatchSize = ivec2((extent.width + 7) / 8, (extent.height + 7) / 8);
                PushConstant pushConstant{.params = glm::vec4(extent.width, extent.height, i, 0)};
                g_context->bindPushConstants(pushConstant).flushAndDispatch(context.commandBuffer, dispatchSize.x, dispatchSize.y, 1);
                extent.width  = std::max(1u, extent.width / 2);
                extent.height = std::max(1u, extent.height / 2);

                if (i == mip - 1) {
					VkImageMemoryBarrier2 barrier{};
					barrier.sType             = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
					barrier.image             = hierrachy.getVkImage().getHandle();
					barrier.oldLayout         = VK_IMAGE_LAYOUT_GENERAL;
					barrier.newLayout         = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.subresourceRange  = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};
					barrier.srcAccessMask     = VK_ACCESS_SHADER_WRITE_BIT;
					barrier.dstAccessMask     = VK_ACCESS_SHADER_READ_BIT;
					barrier.srcStageMask      = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					barrier.dstStageMask      = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					VkDependencyInfo dependencyInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dependencyInfo.imageMemoryBarrierCount = 1;
                    dependencyInfo.pImageMemoryBarriers    = &barrier;
                    vkCmdPipelineBarrier2(context.commandBuffer.getHandle(), &dependencyInfo);
				}
            }
        });
}
