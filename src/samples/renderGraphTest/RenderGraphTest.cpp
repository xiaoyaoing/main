//
// Created by pc on 2023/8/17.
//

#include "RenderGraphTest.h"
#include "Core/Shader/Shader.h"
#include "../../framework/Common/VkCommon.h"
#include "Common/Config.h"
#include "Common/FIleUtils.h"
#include "Core/View.h"
#include "Core/math.h"
#include "Core/Shader/GlslCompiler.h"
#include "RenderPasses/RasterizationPass.h"
#include "RenderPasses/SSGIPass.h"
#include "RenderPasses/ShadowMapPass.h"
#include "Scene/SceneLoader/SceneLoaderInterface.h"
#include "Scene/SceneLoader/gltfloader.h"

void RenderGraphTest::drawFrame(RenderGraph& rg) {
    rg.setCutUnUsedResources(false);
    for (auto& pass : passes) {
        pass->render(rg);
    }
    if (enableSSR) {
        ssrPass->render(rg);
    }
    return;

    auto& commandBuffer = renderContext->getGraphicCommandBuffer();

    auto& blackBoard = rg.getBlackBoard();

    //ON PASS TWO SUBPASS
    struct OnePassTwoSubPassDeferedShadingData {

        RenderGraphHandle albedo;

        RenderGraphHandle normal;

        RenderGraphHandle depth;

        RenderGraphHandle output;

        RenderPassType type = RenderPassType::GRAPHICS;
    };

    if (useSubpass) {

        rg.addGraphicPass(
            "gbuffer", [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto albedo = rg.createTexture("albedo",
                                                     {
                                                             .extent = renderContext->getViewPortExtent(),
                                                             .useage = TextureUsage::SUBPASS_INPUT |
                                                                       TextureUsage::COLOR_ATTACHMENT
                                                     });

            auto normal = rg.createTexture("normal",
                                                     {
                                                             .extent = renderContext->getViewPortExtent(),
                                                             .useage = TextureUsage::SUBPASS_INPUT |
                                                                       TextureUsage::COLOR_ATTACHMENT

                                                     });

            auto depth = rg.createTexture(DEPTH_IMAGE_NAME, {
                           .extent = renderContext->getViewPortExtent(),
                           .useage = TextureUsage::SUBPASS_INPUT |
                                     TextureUsage::DEPTH_ATTACHMENT

                   });

             auto output = rg.getBlackBoard().getHandle(RENDER_VIEW_PORT_IMAGE_NAME);

            RenderGraphPassDescriptor desc;
            desc.setTextures({output, depth, albedo,normal}).addSubpass({.outputAttachments = {albedo, normal, depth}}).addSubpass({
                                            .inputAttachments = {
                                                    albedo, depth,
                                                    normal
                                            },
                                            .outputAttachments = {output}
                                    });
            builder.declare( desc);
            blackBoard.put("albedo", albedo);
            blackBoard.put("normal", normal);
            blackBoard.put(DEPTH_IMAGE_NAME, depth);
            blackBoard.put(RENDER_VIEW_PORT_IMAGE_NAME, output); }, [&](RenderPassContext& context) {

            view->bindViewBuffer().bindViewShading();

            renderContext->bindScene(commandBuffer,*scene).getPipelineState().setPipelineLayout(*pipelineLayouts.gBuffer).setDepthStencilState({.depthCompareOp =  VK_COMPARE_OP_GREATER});

            uint32_t instance_count= 0;
            for(const auto & primitive : view->getMVisiblePrimitives()) {
                renderContext->flushAndDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, primitive->firstVertex,instance_count++);
            }
            renderContext->nextSubpass(commandBuffer);
            renderContext->getPipelineState().setPipelineLayout(*pipelineLayouts.lighting);
            renderContext->getPipelineState().setDepthStencilState({.depthTestEnable = false});


            renderContext->bindImage(0, blackBoard.getImageView("albedo")).bindImage(1, blackBoard.getImageView(DEPTH_IMAGE_NAME)).bindImage(2, blackBoard.getImageView("normal"));

            renderContext->getPipelineState().setRasterizationState({
                                                                            .cullMode = VK_CULL_MODE_NONE
                                                                    });
            renderContext->flushAndDraw(commandBuffer, 3, 1, 0, 0); });
    }
}

void RenderGraphTest::prepare() {
    GlslCompiler::forceRecompile = true;
    Application::prepare();
    passes.emplace_back(std::make_unique<GBufferPass>());
    passes.emplace_back(std::make_unique<ShadowMapPass>());
    passes.emplace_back(std::make_unique<LightingPass>());
    ssrPass = std::make_unique<SSGIPass>();

    for (auto& pass : passes) {
        pass->init();
    }
    ssrPass->init();

    sceneLoadingConfig.indexType = VK_INDEX_TYPE_UINT32;
    loadScene(config.getScenePath());
    RenderPtrManangr::Initalize();
    g_manager->putPtr("view", view.get());
}

RenderGraphTest::RenderGraphTest() : Application("Defered Rendering Sponza", FileUtils::getResourcePath("render.json")) {
    addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    GlslCompiler::forceRecompile = true;
}

void RenderGraphTest::onUpdateGUI() {
    gui->checkBox("Use subpasses", &useSubpass);
    gui->checkBox("Enable SSR", &enableSSR);
    for (auto& pass : passes) {
        pass->updateGui();
    }
    ssrPass->updateGui();

}

int main() {
    auto example = new RenderGraphTest();
    example->prepare();
    example->mainloop();
    return 0;
}
