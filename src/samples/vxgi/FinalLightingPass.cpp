#include "FinalLightingPass.h"

#include "ClipmapRegion.h"
#include "imgui.h"
#include "Core/RenderContext.h"
#include "Core/View.h"

// struct FinalLightingParam {
//     vec3  volume_center;			 // 12
//     uint32_t  uRenderingMode;			 // 16
//     float voxel_size;				 // 20
//     float clip_map_resoultion;			 // 24
//     float uTraceStartOffset; 		 // 28
//     float uIndirectDiffuseIntensity; // 32
//     float uAmbientOcclusionFactor; 	 // 36
//     float uMinTraceStepFactor; 		 // 40
//     float uIndirectSpecularIntensity;// 44
//     float uOcclusionDecay; 			 // 48
//     int   uEnable32Cones;
// };

void FinalLightingPass::init() {
    ShaderPipelineKey shaderPaths{"vxgi/voxelConeTracing.vert", "vxgi/voxelConeTracing.frag"};
    mFinalLightingPipelineLayout = std::make_unique<PipelineLayout>(g_context->getDevice(), shaderPaths);
    mRadianceMapSampler          = std::make_unique<Sampler>(g_context->getDevice(), VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FILTER_LINEAR, 0.0f);
    g_manager->putPtr("radiance_map_sampler", mRadianceMapSampler.get());
}
void FinalLightingPass::render(RenderGraph& rg) {
    rg.addGraphicPass(
        "Final Lighting Pass",
        [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto& blackBoard = rg.getBlackBoard();
            auto  radiance   = blackBoard.getHandle("radiance");
            auto  diffuse    = blackBoard.getHandle(ALBEDO_RG);
            auto  normal     = blackBoard.getHandle("normal");
            auto  depth      = blackBoard.getHandle(DEPTH_IMAGE_NAME);
            auto  emission   = blackBoard.getHandle(EMISSION_RG);

            auto output = blackBoard.getHandle(RENDER_VIEW_PORT_IMAGE_NAME);

            builder.readTextures({radiance, diffuse, normal, depth, emission}).writeTexture(output);

            RenderGraphPassDescriptor desc({radiance, diffuse, normal, depth, emission, output}, {.inputAttachments = {diffuse, normal, depth, emission}, .outputAttachments = {output}});
            builder.declare(desc);
        },
        [&](RenderPassContext& context) {
            auto& view = *g_manager->fetchPtr<View>("view");
            view.bindViewBuffer().bindViewShading();

            auto& blackBoard  = rg.getBlackBoard();
            auto& radianceMap = blackBoard.getHwImage("radiance");

            g_context->getPipelineState().setPipelineLayout(*mFinalLightingPipelineLayout).setDepthStencilState({.depthTestEnable = false}).setRasterizationState({.cullMode = VK_CULL_MODE_NONE});
            g_context->bindImage(0, blackBoard.getImageView(ALBEDO_RG))
                //  .bindImage(1, blackBoard.getImageView("specular"))
                .bindImage(1, blackBoard.getImageView("normal"))
                .bindImage(2, blackBoard.getImageView(EMISSION_RG))
                .bindImage(3, blackBoard.getImageView(DEPTH_IMAGE_NAME))
                .bindImageSampler(0, radianceMap.getVkImageView(), *mRadianceMapSampler);
            pushFinalLightingParam();
            g_context->flushAndDraw(context.commandBuffer, 3, 1, 0, 0);
        });
}

void FinalLightingPass::pushFinalLightingParam() {
    auto& region = VxgiContext::getClipmapRegions()[0];

    mVoxelParam.volume_center       = region.getBoundingBox().center();
    mVoxelParam.voxel_size          = region.voxelSize;
    mVoxelParam.clip_map_resoultion = VOXEL_RESOLUTION;
    mVoxelParam.debugMode           = static_cast<uint32_t>(_renderingMode);

    g_context->bindPushConstants(mVoxelParam);
}

void FinalLightingPass::updateGui() {
    ImGui::SliderFloat("Direct Lighting", &mVoxelParam.uDirectLighting, 0.0f, 10.0f);
    ImGui::SliderFloat("Indirect diffuse intensity", &mVoxelParam.uIndirectDiffuseIntensity, 0.0f, 20.0f);
    ImGui::SliderFloat("Indirct specular intensity", &mVoxelParam.uIndirectSpecularIntensity, 0.0f, 10.0f);
    ImGui::SliderFloat("Opacity scale", &mVoxelParam.fopacityScale, 0.0f, 10.0f);
    ImGui::SliderFloat("trace start offset", &mVoxelParam.uTraceStartOffset, 0.0f, 10.0f);
    ImGui::SliderFloat("max trace distance", &mVoxelParam.maxTraceDistance, 0.0f, 50.0f);
    ImGui::Checkbox("Enable 32 cones", reinterpret_cast<bool*>(&mVoxelParam.uEnable32Cones));
    constexpr const char* kRenderingModeLabels[] = {
        "Diffuse-Only", "Specular-Only", "Normal-Only", "VCT-Start-Level-Only", "DirectContribution", "IndirectDiffuse", "IndirectSpecular", "AmbientOcclsion", "CombinedGI"};
    static int currentItem{static_cast<int>(RenderingMode::CombinedGI)};

    ImGui::Combo("VCT Rendering Mode", &currentItem, kRenderingModeLabels, sizeof(kRenderingModeLabels) / sizeof(const char*));
    _renderingMode = static_cast<RenderingMode>(currentItem);
}
