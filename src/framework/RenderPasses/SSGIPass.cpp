#include "SSGIPass.h"

#include "HizPass.h"
#include "imgui.h"
#include "Common/ResourceCache.h"
#include "Common/TextureHelper.h"
#include "Core/RenderContext.h"
#include "RenderGraph/RenderGraph.h"

void SSGIPass::render(RenderGraph& rg) {
    if (mResource->depth_hierrachy == nullptr) {
        VkExtent3D extent          = g_context->getViewPortExtent3D();
        uint32_t   mipLevels       = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
        mResource->depth_hierrachy = std::make_unique<SgImage>(rg.getDevice(), "depth_hiz", extent, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, VK_IMAGE_VIEW_TYPE_2D, VK_SAMPLE_COUNT_1_BIT, mipLevels, 1);
    }
    rg.importTexture("depth_hiz", mResource->depth_hierrachy.get());

    HizPass::render(rg);

    rg.addComputePass(
        "ssgi", [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            auto& blackBoard = rg.getBlackBoard();
            auto  depth      = blackBoard[DEPTH_IMAGE_NAME];
            auto  depth_hiz  = blackBoard["depth_hiz"];
            auto  normal     = blackBoard[NORMAL_RG];
            auto  diffuse    = blackBoard[ALBEDO_RG];
            auto  output     = blackBoard.getHandle(RENDER_VIEW_PORT_IMAGE_NAME);
            auto  ssgi       = rg.createTexture("ssgi",
                                                {
                                                    .extent = g_context->getViewPortExtent(),
                                                    .useage = TextureUsage::SAMPLEABLE | TextureUsage::STORAGE | TextureUsage::TRANSFER_SRC,

                                         });
            builder.readTextures({output, depth, depth_hiz, normal, diffuse}, RenderGraphTexture::Usage::SAMPLEABLE);
            builder.writeTexture(ssgi, RenderGraphTexture::Usage::STORAGE);

            settings.pipelineLayout = mPipelineLayout.get();
            // builder.addSubPass();
        },
        [&](RenderPassContext& context) {
            auto& commandBuffer = context.commandBuffer;
            auto  view          = g_manager->fetchPtr<View>("view");
            auto& blackBoard    = rg.getBlackBoard();
            view->bindViewBuffer();
            auto& sampler = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FILTER_LINEAR, 1);

            mPushConstant.screen_size       = ivec2(g_context->getViewPortExtent().width, g_context->getViewPortExtent().height);
            mPushConstant.use_inverse_depth = view->getCamera()->useInverseDepth;
            mPushConstant.hiz_mip_count     = mResource->depth_hierrachy->getVkImage().getMipLevelCount();
            auto&      hizDepth             = rg.getBlackBoard().getImageView("depth_hiz");
            auto&      hizSampler           = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_FILTER_LINEAR, hizDepth.getSubResourceRange().levelCount);
            glm::ivec2 dispatchSize         = glm::ivec2((g_context->getViewPortExtent().width + 7) / 8, (g_context->getViewPortExtent().height + 7) / 8);
            g_context->bindImage(0, blackBoard.getImageView("ssgi"))
                .bindPushConstants(mPushConstant);
            bool useCombinedSampler = false;
            if (useCombinedSampler) {
                g_context->bindImageSampler(0, blackBoard.getImageView(ALBEDO_RG), sampler)
                     .bindImageSampler(1, blackBoard.getImageView(NORMAL_RG), sampler)
                    .bindImageSampler(3, blackBoard.getImageView(DEPTH_IMAGE_NAME), sampler)
                    .bindImageSampler(4, blackBoard.getImageView(RENDER_VIEW_PORT_IMAGE_NAME), sampler)
                    .bindImageSampler(5, TextureHelper::GetBlueNoise()->getVkImageView(), sampler)
                    .bindImageSampler(6, hizDepth, hizSampler);
            } else {
                g_context->bindImage(0,blackBoard.getImageView(ALBEDO_RG),1).bindImage(1, blackBoard.getImageView(NORMAL_RG),1)
                    .bindImage(3, blackBoard.getImageView(DEPTH_IMAGE_NAME),1)
                    .bindImage(4, blackBoard.getImageView(RENDER_VIEW_PORT_IMAGE_NAME),1)
                    .bindImage(5, TextureHelper::GetBlueNoise()->getVkImageView(),1)
                    .bindImage(6, hizDepth,1)
                    .bindSampler(7, sampler);
            }
            g_context->flushAndDispatch(commandBuffer, dispatchSize.x, dispatchSize.y, 1);
        });

    rg.addImageCopyPass(rg.getBlackBoard().getHandle("ssgi"), rg.getBlackBoard().getHandle(RENDER_VIEW_PORT_IMAGE_NAME));
}
void SSGIPass::init() {
    PassBase::init();
    mResource = std::make_unique<SSRResource>();
    //mPipelineLayout = std::make_unique<PipelineLayout>(g_context->getDevice(), ShaderPipelineKey{"postprocess/ssgi.comp"});
    ShaderKey key("postprocess/ssr.hlsl");
    key.stage             = VK_SHADER_STAGE_COMPUTE_BIT;
    mPipelineLayout       = std::make_unique<PipelineLayout>(g_context->getDevice(), ShaderPipelineKey{key});
    mPushConstant.use_hiz = 2;
}
void SSGIPass::updateGui() {
    PassBase::updateGui();
    ImGui::SliderInt("Use hiz", reinterpret_cast<int*>(&mPushConstant.use_hiz), 0, 2);
    ImGui::SliderFloat("Depth thickness", &mPushConstant.depth_buffer_thickness, 0.0f, 0.1f);
    ImGui::Checkbox("Show original", reinterpret_cast<bool*>(&mPushConstant.show_original));
}
void SSGIPass::update() {
    PassBase::update();
}
