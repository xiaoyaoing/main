#include "View.h"

#include "imgui.h"
#include "RenderContext.h"
#include "Gui/Gui.h"
#include "Scene/Scene.h"
#include "Scene/Compoments/SgLight.h"

constexpr size_t CONFIG_MAX_LIGHT_COUNT = 64;


View::View(Device& device) {
    mLightBuffer.resize(g_context->getSwapChainImageCount());
    for (auto& lightBuffer : mLightBuffer) {
        lightBuffer = std::make_unique<Buffer>(device, sizeof(LightUib) * CONFIG_MAX_LIGHT_COUNT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    }
    mPerViewBuffer = std::make_unique<Buffer>(device, sizeof(PerViewUnifom), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}
void View::setScene(const Scene* scene) {
    mScene = scene;

    for (const auto& primitive : mScene->getPrimitives()) {
        mVisiblePrimitives.emplace_back(primitive.get());
    }
    for (const auto& texture : mScene->getTextures()) {
        mImageViews.emplace_back(&texture->getImage().getVkImageView());
        mSamplers.emplace_back(&texture->getSampler());
    }
    mMaterials = scene->getGltfMaterials();

    mLights = scene->getLights();
    lightDirty = true;
    updateLight(true);
}
void View::setCamera(const Camera* camera) {
    mCamera = camera;
}

View& View::bindViewBuffer() {
    PerViewUnifom perViewUnifom{};
    perViewUnifom.view_proj     = mCamera->viewProj();
    perViewUnifom.proj          = mCamera->proj();
    perViewUnifom.view          = mCamera->view();
    perViewUnifom.inv_view_proj = glm::inverse(perViewUnifom.view_proj);
    perViewUnifom.inv_proj      = glm::inverse(perViewUnifom.proj);
    perViewUnifom.inv_view      = glm::inverse(perViewUnifom.view);

    perViewUnifom.znear = mCamera->getNearClipPlane();
    perViewUnifom.zfar = mCamera->getFarClipPlane();


    perViewUnifom.resolution     = glm::ivec2(g_context->getViewPortExtent().width, g_context->getViewPortExtent().height);
    perViewUnifom.inv_resolution = glm::vec2(1.0f / perViewUnifom.resolution.x, 1.0f / perViewUnifom.resolution.y);
    perViewUnifom.light_count    = getLights().size();
    perViewUnifom.camera_pos     = mCamera->getPosition();
    perViewUnifom.roughnessScale = mPerViewUniform.roughnessScale;
    perViewUnifom.normalScale    = mPerViewUniform.normalScale;
    perViewUnifom.roughnessOverride = mPerViewUniform.roughnessOverride;
    perViewUnifom.overrideRoughness = mPerViewUniform.overrideRoughness;

    if (perViewUnifom != mPerViewUniform) {
        mPerViewBuffer->uploadData(&perViewUnifom, sizeof(PerViewUnifom), 0);
        mPerViewUniform = perViewUnifom;
    }
    //perViewUnif

    g_context->bindBuffer(static_cast<uint32_t>(UniformBindingPoints::PER_VIEW), *mPerViewBuffer, 0, mPerViewBuffer->getSize(), 0);

    return *this;
}

View& View::bindViewShading() {

    if(lightDirty) updateLight();
    g_context->bindBuffer(static_cast<uint32_t>(UniformBindingPoints::LIGHTS), *mLightBuffer[g_context->getActiveFrameIndex()], 0, mLightBuffer[g_context->getActiveFrameIndex()]->getSize(), 0);

    auto             materials  = GetMMaterials();
    BufferAllocation allocation = g_context->allocateBuffer(sizeof(GltfMaterial) * materials.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    allocation.buffer->uploadData(materials.data(), allocation.size, allocation.offset);
    g_context->bindBuffer(3, *allocation.buffer, allocation.offset, 0);

    for (uint32_t i = 0; i < mImageViews.size(); i++) {
        g_context->bindImageSampler(6, *mImageViews[i], *mSamplers[i],0,i);
    }

    //bind shadow maps
    return *this;
}
int View::bindTexture(const ImageView& imageView, const Sampler& sampler) {
    mImageViews.emplace_back(&imageView);
    mSamplers.emplace_back(&sampler);
    return mImageViews.size() - 1;
}

View& View::bindViewGeom(CommandBuffer& commandBuffer) {
    if (mScene->getBufferRate() == BufferRate::PER_SCENE)
        g_context->bindScene(commandBuffer, *mScene);
    return *this;
}
void View::drawPrimitives(CommandBuffer& commandBuffer) {
    if (mScene->getBufferRate() == BufferRate::PER_PRIMITIVE) {
        drawPrimitivesUseSeparateBuffers(commandBuffer);
        return;
    }
    uint32_t instance_count = 0;

    if(mScene->getMergeDrawCall()) {
        uint32 indexCount = 0;
        for (const auto& primitive : mVisiblePrimitives) {
            indexCount += primitive->indexCount;
        }
        g_context->flushAndDrawIndexed(commandBuffer, indexCount, 1, 0, 0, instance_count);

    }
    else {
        for (const auto& primitive : mVisiblePrimitives) {
            g_context->flushAndDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, primitive->firstVertex, instance_count++);
        }
    }

}
void View::drawPrimitives(CommandBuffer& commandBuffer, const PrimitiveSelectFunc& selectFunc,bool forceSelect) {
    if(mScene->getMergeDrawCall()) {
        if(forceSelect)
            LOGE("forceSelect is not supported in merge draw call mode");
        return drawPrimitives(commandBuffer);
    }

    int instance_count = 0;
    for (const auto& primitive : mVisiblePrimitives) {
        if (selectFunc(*primitive)) {
            g_context->flushAndDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, primitive->firstVertex, instance_count);
        }
        instance_count++;
    }
}
void View::drawPrimitivesUseSeparateBuffers(CommandBuffer& commandBuffer) {
    uint32_t instance_count = 0;
    //Here first binding hard code
    //Fix me
    commandBuffer.bindVertexBuffer(3, mScene->getPrimitiveIdBuffer(), {0});
    g_context->bindBuffer(static_cast<uint32_t>(UniformBindingPoints::PRIM_INFO), mScene->getUniformBuffer(), 0, mScene->getUniformBuffer().getSize());
    for (const auto& primitive : mVisiblePrimitives) {
        g_context->bindPrimitiveGeom(commandBuffer, *primitive);
        g_context->flushAndDrawIndexed(commandBuffer, primitive->indexCount, 1, 0, 0, instance_count++);
        //  return;
    }
}

void View::updateGui() {
    ImGui::Begin("View");
    ImGui::Text("Lights: %d", mLights.size());
    for (auto& light : mLights) {
        //todo fix this only one dir now
        static float dir[3] = {light.lightProperties.direction.x, light.lightProperties.direction.y, light.lightProperties.direction.z};
        ImGui::SliderFloat3("Direction", dir, -1, 1, "%.2f");
        static float pos[3] = {light.lightProperties.position.x, light.lightProperties.position.y, light.lightProperties.position.z};
        ImGui::SliderFloat3("Position", pos, -100, 100, "%.2f");
        ImGui::Checkbox("Use shadow", &light.lightProperties.use_shadow);
        light.lightProperties.direction = glm::normalize(glm::vec3(dir[0], dir[1], dir[2]));
        light.lightProperties.position = glm::vec3(pos[0], pos[1], pos[2]);
    }
    ImGui::SliderFloat("Rough Scale", &mPerViewUniform.roughnessScale, 0.0f, 5.0f);
    ImGui::SliderFloat("Normal Scale", &mPerViewUniform.normalScale, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness Override value", &mPerViewUniform.roughnessOverride, 0.0f, 1.0f);
    ImGui::Checkbox("Override Roughness", reinterpret_cast<bool *>(&mPerViewUniform.overrideRoughness));
    ImGui::End();
}
void View::perFrameUpdate() {
    mSamplers.resize(mScene->getTextures().size());
    mImageViews.resize(mScene->getTextures().size());
}

const Camera* View::getCamera() const {
    return mCamera;
}
void View::IteratorPrimitives(const PrimitiveCallBack& callback) const {
    for ( auto& primitive : mVisiblePrimitives) {
        callback(*primitive);
    }
}

std::vector<Primitive*> View::getMVisiblePrimitives() const {
    return mVisiblePrimitives;
}
void View::setMVisiblePrimitives(const std::vector<Primitive*>& mVisiblePrimitives) {
    this->mVisiblePrimitives = mVisiblePrimitives;
}

std::vector<SgLight>& View::getLights() {
    return mLights;
}
AlphaMode View::getAlphaMode(const Primitive& primitive) const {
    return static_cast<AlphaMode>(mMaterials[primitive.materialIndex].alphaMode);
}

std::vector<GltfMaterial> View::GetMMaterials() const {
    return mMaterials;
}
void View::updateLight(bool updateAllLightBuffer) {
    if(getLights().empty())
        return;
    std::vector<LightUib> lights;
    lights.reserve(getLights().size());
    for (const auto& light : getLights()) {
        LightUib lightUib;
        switch (light.type) {
            case LIGHT_TYPE::Point:
                lightUib = {
                .color     = glm::vec4(light.lightProperties.color, light.lightProperties.intensity),
                .position  = glm::vec4(light.lightProperties.position, 1),
                .direction = glm::vec4(0.0f),
                .info      = glm::vec4(2)};
            break;
            case LIGHT_TYPE::Directional:
                lightUib = {
                .color     = glm::vec4(light.lightProperties.color, light.lightProperties.intensity),
                .direction = glm::vec4(light.lightProperties.direction, 1),
                .info      = glm::vec4(1)};
            break;
            case LIGHT_TYPE::Spot:
                lightUib = {
                .color     = glm::vec4(light.lightProperties.color, light.lightProperties.intensity),
                .position  = glm::vec4(light.lightProperties.position, 1),
                .direction = glm::vec4(light.lightProperties.direction, 2),
                .info      = glm::vec4(light.lightProperties.inner_cone_angle, light.lightProperties.outer_cone_angle, 0, 0)};
            break;
            default:
                break;//todo
        }
        lightUib.info.z = light.lightProperties.shadow_index;
        lightUib.shadow_matrix = light.lightProperties.shadow_matrix;
        lightUib.shadow_desc = light.lightProperties.shadow_desc;
        lights.emplace_back(lightUib);
    }
    if(!updateAllLightBuffer) {
        auto & currentLightBuffer = mLightBuffer[g_context->getActiveFrameIndex()];
        currentLightBuffer->uploadData(lights.data(), lights.size() * sizeof(LightUib), 0);
    }
    else {
        for (uint32_t i = 0; i < g_context->getSwapChainImageCount(); i++) {
            mLightBuffer[i]->uploadData(lights.data(), lights.size() * sizeof(LightUib), 0);
        }
    }
    lightDirty = false;
}
