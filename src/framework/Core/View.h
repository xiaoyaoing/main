#pragma once
#include "Buffer.h"
// #include "RenderContext.h"
#include "Scene/Compoments/Camera.h"
#include "Scene/Compoments/RenderPrimitive.h"
#include "Scene/Compoments/SgLight.h"

class CommandBuffer;
struct GltfMaterial;
class Scene;
class Texture;
class ImageView;
class Sampler;

enum AlphaMode {
    OPAQUE = 0,
    MASK,
    BLEND
};

class View {
public:
    View(Device& device);
    void  setScene(const Scene* scene);
    void  setCamera(const Camera* camera);
    View& bindViewBuffer();
    View& bindViewBuffer(const SgLight & light);
    View& bindViewShading();
    int   bindTexture(const ImageView& imageView, const Sampler& sampler);
    View& bindViewGeom(CommandBuffer& commandBuffer);

    void drawPrimitives(CommandBuffer& commandBuffer);

    using PrimitiveSelectFunc = std::function<bool(const Primitive& primitive)>;
    void drawPrimitives(CommandBuffer& commandBuffer, const PrimitiveSelectFunc& selectFunc,bool forceSelect = false);

    void drawPrimitivesUseSeparateBuffers(CommandBuffer& commandBuffer);
    void setLightDirty(bool dirty) { lightDirty = dirty; }

    void updateGui();
    void perFrameUpdate();

    const Camera*                 getCamera() const;

    using PrimitiveCallBack = std::function<void(Primitive& primitive)>;
    void IteratorPrimitives(const PrimitiveCallBack& callback) const;
    std::vector< Primitive*> getMVisiblePrimitives() const;
    void                          setMVisiblePrimitives(const std::vector< Primitive*>& mVisiblePrimitives);
    std::vector<SgLight> & getLights();

    AlphaMode getAlphaMode(const Primitive & primitive) const;
    std::vector<GltfMaterial>   GetMMaterials() const;

protected:
    struct PerViewUnifom {
        glm::mat4 view_proj;
        glm::mat4 inv_view_proj;
        glm::mat4 proj;
        glm::mat4 view;
        glm::mat4 inv_proj;
        glm::mat4 inv_view;

        float zfar;
        float znear;
        float padding1;
        float padding2;

        glm::vec3 camera_pos;
        uint32_t  light_count;

        glm::ivec2 resolution;
        glm::ivec2 inv_resolution;

        float roughnessScale = 1.f;
        float normalScale= 1.f;
        float roughnessOverride = -1.f;
        int overrideRoughness = 0;


        bool operator!=(const PerViewUnifom& other) const {
            return memcmp(this, &other, sizeof(PerViewUnifom)) != 0;
        }
    };

protected:

    void updateLight(bool updateAllLightBuffer = false);

    const Camera*                 mCamera{nullptr};
    std::vector< Primitive*> mVisiblePrimitives;
    std::vector<const ImageView *> mImageViews;
    std::vector<const Sampler *>  mSamplers;
    std::vector<SgLight> mLights;
    std::vector<GltfMaterial>     mMaterials;
    const Scene*                  mScene{nullptr};
    std::unique_ptr<Buffer>       mPerViewBuffer;
    PerViewUnifom                 mPerViewUniform;
    std::vector<std::unique_ptr<Buffer>>      mLightBuffer;

    bool lightDirty{false};
};
