﻿#include "ShadowMapPass.h"

#include "Common/ResourceCache.h"
#include "Core/RenderContext.h"
#include "Core/View.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/Scene.h"

static mat4 getLightMVP(const glm::vec3& lightPos, const glm::vec3& lightDir) {
    glm::mat4 lightView       = glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0.0f, 1.0f, 0.0f));
    float aspect = (float)g_context->getViewPortExtent().width / (float)g_context->getViewPortExtent().height;
    float near = 1.0f;
    float far = 25.0f;
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, near, far);
    return shadowProj * lightView;
}

ShadowMapPass::ShadowMapPass():mSampler(g_context->getDevice().getResourceCache().requestSampler()) {
}
void ShadowMapPass::render(RenderGraph& rg) {
   auto view = g_manager->fetchPtr<View>("view");
    Scene * g_scene;
   auto lights = g_scene->getLights();
    
    for(int i = 0; i < lights.size(); i++) {
        if (lights[i].type == LIGHT_TYPE::Directional) {
            //todo string format
            auto shadowName = std::format("ShadowMap{}", i);
            rg.addGraphicPass(
                "ShadowMap",
                [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
                    auto depth = rg.createTexture(shadowName, {.extent = g_context->getViewPortExtent(),
                .useage = TextureUsage::SUBPASS_INPUT | TextureUsage::DEPTH_ATTACHMENT | TextureUsage::SAMPLEABLE
                                                   });
                builder.declare(RenderGraphPassDescriptor({depth}, {.outputAttachments = {depth}}));
                builder.writeTexture(depth, TextureUsage::DEPTH_ATTACHMENT);
                },
                
                [&](RenderPassContext& context) {
                    g_context->getPipelineState().setPipelineLayout(*mPipelineLayout).setDepthStencilState({.depthCompareOp = VK_COMPARE_OP_LESS});
                    g_context->bindPushConstants(getLightMVP(lights[i].lightProperties.position,lights[i].lightProperties.direction));
                    lights[i].lightProperties.shadow_index = g_manager->getView()->bindTexture(rg.getBlackBoard().getImageView(shadowName), mSampler);
                    g_manager->fetchPtr<View>("view")->bindViewGeom(context.commandBuffer).drawPrimitives(context.commandBuffer);
                    g_manager->getView()->setLightDirty(true);
                });
        }
    }
}
void ShadowMapPass::init() {
    PassBase::init();
    mPipelineLayout = std::make_unique<PipelineLayout>(g_context->getDevice().getResourceCache().requestPipelineLayout({"shadowmap.vert", "shadowmap.frag"}));
}
