//
// Created by pc on 2023/8/17.
//

#include "PbrLab.h"
#include "PbrLab.h"

#include "ctpl_stl.h"
#include "Core/Shader/Shader.h"
#include "../../framework/Common/VkCommon.h"
#include "Common/FIleUtils.h"
#include "Common/ResourceCache.h"
#include "Core/View.h"
#include "Core/math.h"
#include "Core/Shader/GlslCompiler.h"
#include "RenderPasses/GBufferPass.h"
#include "RenderPasses/SSGIPass.h"
#include "RenderPasses/ShadowMapPass.h"
#include "Scene/SceneLoader/SceneLoaderInterface.h"

struct SkyBoxPushConstant {
    vec4  _pad0;
    float exposure;
    float gamma;
};

void VXGI::drawFrame(RenderGraph& rg) {

    // if (scene->getName().find("car.gltf") != std::string::npos) {
    //     view->IteratorPrimitives([](Primitive& primitive) {
    //         primitive.transform.setLocalToWorldMatrix(glm::rotate(0.002f, glm::vec3(0, 1, 0)) * primitive.transform.getLocalToWorldMatrix());
    //     });
    //     scene->updateSceneUniformBuffer();
    // }

    rg.setCutUnUsedResources(false);

    if (environmentCubeAsync) {
        environmentCube = std::move(environmentCubeAsync);
        environmentCubeAsync.reset();
        ibl->setEnvironmentCube(environmentCube.get());
    }
    ibl->importTexturesToRenderGraph(rg);
    ibl->generate(rg);

    rg.addGraphicPass(
        "",
        [&](RenderGraph::Builder& builder, GraphicPassSettings& settings) {
            auto swapchainImage = rg.getBlackBoard().getHandle(RENDER_VIEW_PORT_IMAGE_NAME);
            builder.writeTexture(swapchainImage);
            RenderGraphPassDescriptor descriptor;
            descriptor.textures = {swapchainImage};
            descriptor.addSubpass({.outputAttachments = {swapchainImage}, .disableDepthTest = true});
            builder.declare(descriptor);
        },
        [&](RenderPassContext& context) {
            renderContext->getPipelineState().setDepthStencilState({.depthTestEnable = false}).setRasterizationState({.depthClampEnable = VK_FALSE, .cullMode = VK_CULL_MODE_NONE}).setPipelineLayout(device->getResourceCache().requestPipelineLayout(std::vector<std::string>{"skybox.vert", "skybox.frag"}));
            view->bindViewBuffer();

            renderContext->bindPrimitiveGeom(context.commandBuffer, *cube).bindImageSampler(0, environmentCube->getImage().getVkImageView(), environmentCube->getSampler()).bindPushConstants(SkyBoxPushConstant{.exposure = exposure, .gamma = gamma});
            renderContext->flushAndDrawIndexed(context.commandBuffer, cube->indexCount, 1, 0, 0, 0);
        });

    // if (scene->getName().find("car.gltf") != std::string::npos) {
    //     for (auto& pass : mforwardRenderPasses) {
    //         pass->render(rg);
    //     }
    // } else {
        for (auto& pass : mRenderPasses) {
            pass->render(rg);
        }
    // }
}

void VXGI::prepare() {
    Application::prepare();
    GlslCompiler::forceRecompile = true;

    g_context->setFlipViewport(true);
    mforwardRenderPasses.push_back(std::make_unique<ShadowMapPass>());
    mforwardRenderPasses.push_back(std::make_unique<ForwardPass>());

    mRenderPasses.push_back(std::make_unique<GBufferPass>());
    mRenderPasses.push_back(std::make_unique<IBLLightingPass>());
    // mRenderPasses.push_back(std::make_unique<SSGIPass>());

    sceneLoadingConfig.indexType = VK_INDEX_TYPE_UINT32;
    // loadScene(FileUtils::getResourcePath("cars/car.gltf"));
    loadScene("E:/code/car/resources/scenes/bistro/bistro.gltf");
    // loadScene(FileUtils::getResourcePath("scenes/sponza/Sponza01.gltf"));
    scene->addDirectionalLight({0, -0.5f, -0.12f}, glm::vec3(1.0f), 1.5f, vec3(0, 20, 0));

    RenderPtrManangr::Initalize();
    g_manager->putPtr("view", view.get());

    for (auto& pass : mRenderPasses) {
        pass->init();
    }

    for (auto& pass : mforwardRenderPasses) {
        pass->init();
    }

    cube             = SceneLoaderInterface::loadSpecifyTypePrimitive(*device, "cube");
    std::string path = FileUtils::getResourcePath("pisa_cube.ktx");

    environmentCube = Texture::loadTextureFromFile(g_context->getDevice(), path);
    ibl             = std::make_unique<IBL>(*device, environmentCube.get());
}

VXGI::VXGI() : Application("Pbr Lab", 1920, 1080) {
    addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
}

void VXGI::onUpdateGUI() {
    for (auto& pass : mRenderPasses) {
        pass->updateGui();
    }

    auto file = gui->showFileDialog("Select a cubemap", {".ktx"});

    if (file != "no file selected") {
        ctpl::thread_pool pool(1);
        pool.push([this, file](size_t) {
            LOGI("file selected: {}", file);
            environmentCubeAsync = Texture::loadTextureFromFile(g_context->getDevice(), file);
        });
    }

    // ImGui::Selectable()
}

int main() {
    auto example = new VXGI();
    example->prepare();
    example->mainloop();
    return 0;
}