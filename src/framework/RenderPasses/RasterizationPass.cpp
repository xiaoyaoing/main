#include "RasterizationPass.h"

#include "imgui.h"
#include "Common/ResourceCache.h"
#include "Core/RenderContext.h"
#include "Core/View.h"

struct IBLLightingPassPushConstant {
    float exposure        = 4.5f;
    float gamma           = 2.2f;
    float scaleIBLAmbient = 1.0f;
    float prefilteredCubeMipLevels;
    int   debugMode;
    int   padding[3];
};

void GBufferPass::init() {
    // mNormal = std::make_unique<SgImage>(device,NORMAL_RG,VKExt, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    Device& device  = g_context->getDevice();
}

void VBufferPass::render(RenderGraph& rg) {
    auto& renderContext = g_context;
    rg.addGraphicPass(
        "VBufferPass",
        [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            // primitive id buffer (R32_UINT)
            auto primitive = rg.createTexture(VBUFFER_RG,
                {.extent = renderContext->getViewPortExtent(),
                 .useage = TextureUsage::COLOR_ATTACHMENT |
                          TextureUsage::SAMPLEABLE,                 .format = VK_FORMAT_R32_UINT,
});

            // normal buffer (RGBA16_SFLOAT for normal.xyz + padding)
            auto normal = rg.createTexture(NORMAL_RG,
                {.extent = renderContext->getViewPortExtent(),

                 .useage = TextureUsage::COLOR_ATTACHMENT |
                          TextureUsage::SAMPLEABLE, .format = VK_FORMAT_R16G16B16A16_SFLOAT});

            // uv buffer (RG16_SFLOAT)
            auto uv = rg.createTexture(UV_RG,
                {.extent = renderContext->getViewPortExtent(),
                 .useage = TextureUsage::COLOR_ATTACHMENT |
                          TextureUsage::SAMPLEABLE,                 .format = VK_FORMAT_R16G16_SFLOAT});

            // depth buffer
            auto depth = rg.createTexture(DEPTH_IMAGE_NAME,
                {.extent = renderContext->getViewPortExtent(),
                 .useage = TextureUsage::DEPTH_ATTACHMENT |
                          TextureUsage::SAMPLEABLE,.format = VK_FORMAT_D32_SFLOAT});

            // 设置渲染目标
            RenderGraphPassDescriptor desc({primitive, normal, uv, depth},
                {.outputAttachments = {primitive, normal, uv, depth}});
            builder.declare(desc);

            // 声明资源写入
            builder.writeTextures({primitive, normal, uv}, TextureUsage::COLOR_ATTACHMENT)
                  .writeTexture(depth, TextureUsage::DEPTH_ATTACHMENT);
        },
        [&](RenderPassContext& context) {
            // 绑定VBuffer着色器
            renderContext->bindShaders({"defered_one_scene_buffer.vert", "vbuffer.frag"});

            // 设置渲染状态
            renderContext->getPipelineState()
                .setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_LESS})
                .setRasterizationState({.cullMode = VK_CULL_MODE_NONE});

            // 绑定视图数据并绘制
            g_manager->fetchPtr<View>("view")
                ->bindViewBuffer()
                .bindViewShading()
                .bindViewGeom(context.commandBuffer)
                .drawPrimitives(context.commandBuffer);
        });
}

void VBufferPass::init() {
    PassBase::init();
}

void LightingPass::render(RenderGraph& rg) {
    rg.addGraphicPass(
        "LightingPass", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto& blackBoard = rg.getBlackBoard();
            auto  depth      = blackBoard[DEPTH_IMAGE_NAME];
            auto  normal     = blackBoard[NORMAL_RG];
            auto  diffuse    = blackBoard[ALBEDO_RG];
            auto  emission   = blackBoard[EMISSION_RG];
            auto  output     = blackBoard.getHandle(RENDER_VIEW_PORT_IMAGE_NAME);

            builder.readTextures({depth, normal, diffuse, emission});
            builder.writeTexture(output);

            RenderGraphPassDescriptor desc{};
            desc.setTextures({output, diffuse, depth, normal, emission}).addSubpass({.inputAttachments = {diffuse, depth, normal, emission}, .outputAttachments = {output}, .disableDepthTest = true});
            builder.declare(desc);
            // builder.addSubPass();
        },
        [&](RenderPassContext& context) {
            auto& commandBuffer = context.commandBuffer;
            auto  view          = g_manager->fetchPtr<View>("view");
            auto& blackBoard    = rg.getBlackBoard();
            g_context->getPipelineState().setPipelineLayout(*mPipelineLayout).setRasterizationState({.cullMode = VK_CULL_MODE_NONE}).setDepthStencilState({.depthTestEnable = false});
            view->bindViewBuffer().bindViewShading();
            g_context->bindImage(0, blackBoard.getImageView(ALBEDO_RG))
                .bindImage(1, blackBoard.getImageView(NORMAL_RG))
                .bindImage(2, blackBoard.getImageView(EMISSION_RG))
                .bindImage(3, blackBoard.getImageView(DEPTH_IMAGE_NAME))
                .flushAndDraw(commandBuffer, 3, 1, 0, 0);
        });
}
void LightingPass::init() {
    ShaderPipelineKey shadersPath{"full_screen.vert", "lighting_pbr.frag"};
    mPipelineLayout = std::make_unique<PipelineLayout>(g_context->getDevice(), shadersPath);
}
void ForwardPass::render(RenderGraph& rg) {
    rg.addGraphicPass(
        "ForwardPass", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto& blackBoard = rg.getBlackBoard();
            auto  output     = blackBoard.getHandle(RENDER_VIEW_PORT_IMAGE_NAME);
            auto  depth = rg.createTexture(DEPTH_IMAGE_NAME, {.extent = g_context->getViewPortExtent(), .useage = TextureUsage::DEPTH_ATTACHMENT | TextureUsage::SAMPLEABLE});

            builder.writeTextures({output}, TextureUsage::COLOR_ATTACHMENT).writeTextures({depth}, TextureUsage::DEPTH_ATTACHMENT);
            builder.readTexture(output);

            RenderGraphPassDescriptor desc{};
            desc.setTextures({output, depth}).addSubpass({.outputAttachments = {output,depth}});
            builder.declare(desc); }, [&](RenderPassContext& context) {
            auto view = g_manager->fetchPtr<View>("view");

            g_context->getPipelineState().setPipelineLayout(*mPipelineLayout);
            view->bindViewBuffer().bindViewShading().bindViewGeom(context.commandBuffer);

            auto& blackBoard     = rg.getBlackBoard();
            auto& irradianceCube = blackBoard.getImageView("irradianceCube");
            auto& prefilterCube  = blackBoard.getImageView("prefilterCube");
            auto& brdfLUT        = blackBoard.getImageView("brdfLUT");

            IBLLightingPassPushConstant constant;
            constant.prefilteredCubeMipLevels = prefilterCube.getImage().getMipLevelCount();
            //constant.debugMode                = debugMode;
            g_context->bindPushConstants(constant);

            auto& irradianceCubeSampler = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, irradianceCube.getImage().getMipLevelCount());
            auto& prefilterCubeSampler  = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, prefilterCube.getImage().getMipLevelCount());
            auto& brdfLUTSampler        = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, 1);
            g_context->bindImageSampler(0, irradianceCube, irradianceCubeSampler).bindImageSampler(1, prefilterCube, prefilterCubeSampler).bindImageSampler(2, brdfLUT, brdfLUTSampler);

            g_context->getPipelineState().setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_LESS});
            view->drawPrimitives(context.commandBuffer, [&view](const Primitive& primitive) { return view->getAlphaMode(primitive) == AlphaMode::OPAQUE; });

            ColorBlendAttachmentState colorBlendAttachmentState{};
            colorBlendAttachmentState.blendEnable = VK_TRUE;
            //Blend mode for glass
            colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachmentState.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachmentState.alphaBlendOp        = VK_BLEND_OP_ADD;

            auto blendState        = g_context->getPipelineState().getColorBlendState();
            blendState.attachments = std::vector<ColorBlendAttachmentState>{colorBlendAttachmentState};
            g_context->getPipelineState().setColorBlendState(blendState);

            g_context->getPipelineState().setDepthStencilState({.depthTestEnable = false});
            view->drawPrimitives(context.commandBuffer, [&view](const Primitive& primitive) { return view->getAlphaMode(primitive) == AlphaMode::BLEND; }); });
}
void ForwardPass::init() {
    PassBase::init();
    ShaderPipelineKey shadersPath{"defered_one_scene_buffer.vert", "forward_lighting.frag"};
    mPipelineLayout = std::make_unique<PipelineLayout>(g_context->getDevice(), shadersPath);
}

void IBLLightingPass::render(RenderGraph& rg) {
    rg.addGraphicPass(
        "IBLLightingPass", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto& blackBoard     = rg.getBlackBoard();
            auto  depth          = blackBoard[DEPTH_IMAGE_NAME];
            auto  normal         = blackBoard[NORMAL_RG];
            auto  diffuse        = blackBoard[ALBEDO_RG];
            auto  emission       = blackBoard[EMISSION_RG];
            auto  output         = blackBoard.getHandle(RENDER_VIEW_PORT_IMAGE_NAME);
            auto  irradianceCube = blackBoard.getHandle("irradianceCube");
            auto  prefilterCube  = blackBoard.getHandle("prefilterCube");
            auto  brdfLUT        = blackBoard.getHandle("brdfLUT");
            // auto  shadow        = blackBoard.getHandle("ShadowMap0");

            builder.readTextures({depth, normal, diffuse, emission, output});
            builder.readTextures({irradianceCube, prefilterCube, brdfLUT}, TextureUsage::SAMPLEABLE);
            builder.writeTexture(output);

            RenderGraphPassDescriptor desc{};
            desc.setTextures({output, diffuse, depth, normal, emission}).addSubpass({.inputAttachments = {diffuse, depth, normal, emission}, .outputAttachments = {output}, .disableDepthTest = true});
            builder.declare(desc);
            // builder.addSubPass();
        },
        [&](RenderPassContext& context) {
            auto& commandBuffer = context.commandBuffer;
            auto  view          = g_manager->fetchPtr<View>("view");
            auto& blackBoard    = rg.getBlackBoard();
            g_context->getPipelineState().setPipelineLayout(g_context->getDevice().getResourceCache().requestPipelineLayout(ShaderPipelineKey{"full_screen.vert", "pbrLab/lighting_ibl.frag"})).setRasterizationState({.cullMode = VK_CULL_MODE_NONE}).setDepthStencilState({.depthTestEnable = false});
            view->bindViewBuffer();

            auto& irradianceCube = blackBoard.getImageView("irradianceCube");
            auto& prefilterCube  = blackBoard.getImageView("prefilterCube");
            auto& brdfLUT        = blackBoard.getImageView("brdfLUT");

            IBLLightingPassPushConstant constant;
            constant.prefilteredCubeMipLevels = prefilterCube.getImage().getMipLevelCount();
            constant.debugMode                = debugMode;
            g_context->bindPushConstants(constant);

            auto& irradianceCubeSampler = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, irradianceCube.getImage().getMipLevelCount());
            auto& prefilterCubeSampler  = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, prefilterCube.getImage().getMipLevelCount());
            auto& brdfLUTSampler        = g_context->getDevice().getResourceCache().requestSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR, 1);
            g_context->bindImageSampler(0, irradianceCube, irradianceCubeSampler).bindImageSampler(1, prefilterCube, prefilterCubeSampler).bindImageSampler(2, brdfLUT, brdfLUTSampler);

            g_manager->getView()->bindViewShading();

            g_context->bindImage(0, blackBoard.getImageView(ALBEDO_RG))
                .bindImage(1, blackBoard.getImageView(NORMAL_RG))
                .bindImage(2, blackBoard.getImageView(EMISSION_RG))
                .bindImage(3, blackBoard.getImageView(DEPTH_IMAGE_NAME))
                .flushAndDraw(commandBuffer, 3, 1, 0, 0);
        });
}

enum class DebugMode {
    NONE = 0,
    DIFFUSE,
    SPECULAR,
    NORMAL,
    DEPTH,
    ALBEDO,
    METALLIC,
    ROUGHNESS,
    AMBIENT_OCCLUSION,
    IRRADIANCE,
    PREFILTER,
    BRDF_LUT
};

void IBLLightingPass::updateGui() {
    ImGui::Combo("Debug Mode", &debugMode, "None\0Diffuse\0Specular\0Normal\0Depth\0Albedo\0Metallic\0Roughness\0Ambient Occlusion\0Irradiance\0Prefilter\0BRDF LUT\0");
}

void GBufferPass::render(RenderGraph& rg) {
    auto& blackBoard    = rg.getBlackBoard();
    auto& renderContext = g_context;
    rg.addGraphicPass(
        "GBufferPass", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto diffuse = rg.createTexture(ALBEDO_RG,
                                            {.extent             = renderContext->getViewPortExtent(),
                                             .useage = TextureUsage::SUBPASS_INPUT |
                                                       TextureUsage::COLOR_ATTACHMENT| TextureUsage::SAMPLEABLE});

            auto normal = rg.createTexture(NORMAL_RG,
                                           {.extent = renderContext->getViewPortExtent(),
                                            .useage = TextureUsage::SUBPASS_INPUT |
                                                      TextureUsage::COLOR_ATTACHMENT | TextureUsage::SAMPLEABLE

                                           });

            auto emission = rg.createTexture(EMISSION_RG,
                                             {.extent = renderContext->getViewPortExtent(),
                                              .useage = TextureUsage::SUBPASS_INPUT |
                                                        TextureUsage::COLOR_ATTACHMENT | TextureUsage::SAMPLEABLE});

            auto depth = rg.createTexture(DEPTH_IMAGE_NAME, {.extent = renderContext->getViewPortExtent(),
                .useage = TextureUsage::SUBPASS_INPUT | TextureUsage::DEPTH_ATTACHMENT | TextureUsage::SAMPLEABLE

                                                   });

         RenderGraphPassDescriptor desc({diffuse,  normal, emission, depth}, {.outputAttachments = {diffuse,  normal, emission, depth}});
            builder.declare(desc);

            builder.writeTextures({diffuse,  emission, depth}, TextureUsage::COLOR_ATTACHMENT).writeTexture(depth, TextureUsage::DEPTH_ATTACHMENT); }, [&](RenderPassContext& context) {
            renderContext->bindShaders({"defered_one_scene_buffer.vert", "defered_pbr.frag"});
            renderContext->getPipelineState().setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_LESS}).setRasterizationState({.cullMode =  VK_CULL_MODE_NONE});
            g_manager->fetchPtr<View>("view")->bindViewBuffer().bindViewShading().bindViewGeom(context.commandBuffer).drawPrimitives(context.commandBuffer); });
}

std::vector<std::string> outputToBufferDefines                  = {"OUTPUT_TO_BUFFER"};
std::vector<std::string> outputToBufferAndDirectLightingDefines = {"OUTPUT_TO_BUFFER", "DIRECT_LIGHTING"};

static ShaderPipelineKey CommonGBuffer                    = {"defered_one_scene_buffer.vert", "defered_pbr.frag"};
static ShaderPipelineKey GBufferToBuffer                  = {"defered_one_scene_buffer.vert", {"defered_pbr.frag", outputToBufferDefines}};
static ShaderPipelineKey GBufferToBufferAndDirectLighting = {"defered_one_scene_buffer.vert", {"defered_pbr.frag", outputToBufferAndDirectLightingDefines}};

void GBufferPass::renderToBuffer(RenderGraph& rg, RenderGraphHandle outputBuffer, RenderGraphHandle directLightingImage) {

    rg.addComputePass("Clear GBuffer", [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        builder.writeBuffer(outputBuffer);
    }, [this,&rg,_outputBuffer = outputBuffer](RenderPassContext& context) {
        auto gbuffer = rg.getBuffer(_outputBuffer);
        g_context->bindBuffer(5, *gbuffer->getHwBuffer());
        g_context->bindShaders({"clearGBuffer.comp"});
        auto bufferCount = g_context->getViewPortExtent().width * g_context->getViewPortExtent().height;
        g_context->flushAndDispatch(context.commandBuffer, (bufferCount + 63) / 64, 1, 1);
    });

    rg.addGraphicPass(
        "GBufferPassToBuffer", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            builder.writeBuffer(outputBuffer,BufferUsage::STORAGE);
            if(directLightingImage.isInitialized()) {
                builder.writeTexture(directLightingImage);
            }
            auto depth = rg.createTexture(DEPTH_IMAGE_NAME, {.extent = g_context->getViewPortExtent(), .useage = TextureUsage::DEPTH_ATTACHMENT | TextureUsage::SAMPLEABLE});
            builder.writeTexture(depth);
            RenderGraphPassDescriptor desc{};
            desc.setTextures(directLightingImage.isInitialized() ? std::vector{ directLightingImage, depth} : std::vector{depth});
            desc.addSubpass({.outputAttachments = desc.textures });
            builder.declare(desc);
        }, [_outputBuffer = outputBuffer,_directLightingImage = directLightingImage,&rg](RenderPassContext& context) {

            g_context->bindBuffer(5,*rg.getBuffer(_outputBuffer)->getHwBuffer());

            g_context->bindShaders(_directLightingImage.isInitialized() ? GBufferToBufferAndDirectLighting : GBufferToBuffer);
            g_context->getPipelineState().setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_LESS}).setRasterizationState({.cullMode =  VK_CULL_MODE_NONE});
            g_manager->fetchPtr<View>("view")->bindViewBuffer().bindViewShading().bindViewGeom(context.commandBuffer).drawPrimitives(context.commandBuffer); });
}
