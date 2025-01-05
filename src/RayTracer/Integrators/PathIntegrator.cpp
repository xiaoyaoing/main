#include "PathIntegrator.h"

#include "imgui.h"
#include "Common/Distrib.hpp"
#include "Common/ResourceCache.h"
#include "Scene/Compoments/Camera.h"
#include "Raytracing/PT/path_commons.h"
#include "RenderPasses/RasterizationPass.h"

#include <numeric>

#define OFFSET(structure, member) ((int64_t) & ((structure*)0)->member)// 64位系统

void PathIntegrator::render(RenderGraph& renderGraph) {
    if (!entry_->primAreaBuffersInitialized) {
        initLightAreaDistribution(renderGraph);
    }
    

    auto& commandBuffer = renderContext->getGraphicCommandBuffer();

    renderGraph.addRaytracingPass(
        "PT pass", [&](RenderGraph::Builder& builder, RaytracingPassSettings& settings) {
        settings.accel = &entry_->tlas;
        settings.pipelineLayout = layout.get();
        settings.rTPipelineSettings.dims = {width,height,1};
        settings.rTPipelineSettings.maxDepth = getPC().max_depth;
    
    
        auto output = renderGraph.getBlackBoard().getHandle(RT_IMAGE_NAME);
        builder.writeTexture(output,TextureUsage::STORAGE); }, [&](RenderPassContext& context) {
            bindRaytracingResources(commandBuffer);
    
            auto pushConstant = toBytes(getPC());
            renderContext->bindPushConstants(pushConstant);
            renderContext->bindImage(0, renderGraph.getBlackBoard().getImageView(RT_IMAGE_NAME));
            renderContext->traceRay(commandBuffer, {width, height, 1});
    
            getPC().frame_num++; });
}

void PathIntegrator::initScene(RTSceneEntry& entry) {
    Integrator::initScene(entry);
    
    getPC().light_num = entry_->lights.size();
    getPC().enable_sample_bsdf  = config.sample_bsdf;
    getPC().enable_sample_light = config.sample_light;
    getPC().enable_accumulation = true;
    getPC().max_depth           = config.max_depth;
    getPC().min_depth           = config.min_depth;
}

void PathIntegrator::onUpdateGUI() {
    int maxDepth = getPC().max_depth;
    int minDepth = getPC().min_depth;
    ImGui::SliderInt("Min Depth", &minDepth, 0, getPC().max_depth);
    ImGui::SliderInt("Max Depth", &maxDepth, 1, 10);
    if (maxDepth != getPC().max_depth) {
        getPC().max_depth = maxDepth;
        getPC().frame_num = 0;
    }
    if (minDepth != getPC().min_depth) {
        getPC().min_depth = minDepth;
        getPC().frame_num = 0;
    }
    ImGui::Text("frame count %5d", getPC().frame_num);
    ImGui::Checkbox("Sample BSDF", reinterpret_cast<bool*>(&getPC().enable_sample_bsdf));
    ImGui::SameLine();
    ImGui::Checkbox("Sample Light", reinterpret_cast<bool*>(&getPC().enable_sample_light));
    ImGui::Checkbox("Enable Accumulation", reinterpret_cast<bool*>(&getPC().enable_accumulation));
    ImGui::Checkbox("Visual Throughput", reinterpret_cast<bool*>(&getPC().visual_throughput));
    ImGui::Checkbox("Visual Normal", reinterpret_cast<bool*>(&getPC().visual_normal));
    ImGui::Checkbox("Visual Material Type", reinterpret_cast<bool*>(&getPC().visual_material_type));
    ImGui::Checkbox("Visual Albedo", reinterpret_cast<bool*>(&getPC().visual_albedo));
}
bool PathIntegrator::resetFrameOnCameraMove() const {
    return true;
}
PathIntegrator::PathIntegrator(Device& device, PathTracingConfig _config) : Integrator(device) {
    layout = std::make_unique<PipelineLayout>(device, ShaderPipelineKey{
                                                          "Raytracing/PT/raygen.rgen",
                                                          "Raytracing/PT/miss.rmiss",
                                                          "Raytracing/PT/miss_shadow.rmiss",
                                                          "Raytracing/PT/closesthit.rchit",
                                                          "Raytracing/ray.rahit",
                                                      });
    config = _config;
   
}

