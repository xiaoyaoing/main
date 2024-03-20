#include "GBufferPass.h"

#include "Core/RenderContext.h"
#include "Core/View.h"
void GBufferPass::init() {
    // mNormal = std::make_unique<SgImage>(device,"normal",VKExt, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    Device&             device = g_context->getDevice();
    std::vector<Shader> shaders{
        Shader(device, FileUtils::getShaderPath("defered_one_scene_buffer.vert")),
        Shader(device, FileUtils::getShaderPath("defered_pbr.frag"))};
    mPipelineLayout = std::make_unique<PipelineLayout>(device, shaders);
}

void GBufferPass::render(RenderGraph& rg) {
    auto& blackBoard    = rg.getBlackBoard();
    auto& renderContext = g_context;
    rg.addPass(
        "GBufferPass", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto diffuse = rg.createTexture("diffuse",
                                            {.extent = renderContext->getSwapChainExtent(),
                                             .useage = TextureUsage::SUBPASS_INPUT |
                                                       TextureUsage::COLOR_ATTACHMENT});

            // auto specular = rg.createTexture("specular",
            //                                  {.extent = renderContext->getSwapChainExtent(),
            //                                   .useage = TextureUsage::SUBPASS_INPUT |
            //                                             TextureUsage::COLOR_ATTACHMENT});
            auto normal = rg.createTexture("normal",
                                           {.extent = renderContext->getSwapChainExtent(),
                                            .useage = TextureUsage::SUBPASS_INPUT |
                                                      TextureUsage::COLOR_ATTACHMENT

                                           });

            auto emission = rg.createTexture("emission",
                                             {.extent = renderContext->getSwapChainExtent(),
                                              .useage = TextureUsage::SUBPASS_INPUT |
                                                        TextureUsage::COLOR_ATTACHMENT});

            auto depth = rg.createTexture("depth", {.extent = renderContext->getSwapChainExtent(), .useage = TextureUsage::SUBPASS_INPUT | TextureUsage::DEPTH_ATTACHMENT

                                                   });
            
         RenderGraphPassDescriptor desc({diffuse,  normal, emission, depth}, {.outputAttachments = {diffuse,  normal, emission, depth}});
            builder.declare(desc);

            builder.writeTextures({diffuse,  emission, depth}, TextureUsage::COLOR_ATTACHMENT).writeTexture(depth, TextureUsage::DEPTH_ATTACHMENT); }, [&](RenderPassContext& context) {
            renderContext->getPipelineState().setPipelineLayout(*mPipelineLayout).setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_GREATER});
            g_manager->fetchPtr<View>("view")->bindViewBuffer().bindViewShading().bindViewGeom(context.commandBuffer).drawPrimitives(context.commandBuffer); });
}