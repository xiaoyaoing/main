#pragma once
#include "Integrator.h"
#include "Common/RenderConfig.h"
#include "Raytracing/PT/path_commons.h"
#include "Raytracing/ddgi/ddgi_commons.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderPasses/RasterizationPass.h"
#include "RenderPasses/ShadowMapPass.h"

#include <variant>

class IBLRenderer{};
class RayTracerRenderer{};
class DeferredRenderer{};
class DDGIRenderer{};



using Renderer = std::variant<IBLRenderer, RayTracerRenderer, DeferredRenderer, DDGIRenderer>;

class DDGIIntegrator : public Integrator {
    void render(RenderGraph& graph) override;

public:
    void init() override;
    void initScene(RTSceneEntry& entry) override;
    DDGIIntegrator(Device& device,DDGIConfig config);
    void onUpdateGUI() override;

protected:
    DDGIConfig config;
    struct DDGIBuffers;
    DDGIBuffers * buffers{nullptr};
    DDGIUbo ubo;
    // PCPath pc_ray;
    uint ping = 0;
    uint pong = 0;
    std::unique_ptr<GBufferPass> gbufferPass;
    std::unique_ptr<ShadowMapPass> shadowMapPass;
    bool debugDDGI = false;
    bool usePointSampler = false;
    bool showIndirect = true;
    bool relocate = false;
    bool showDirect = true;
    uint frameCount = 0;
    std::unique_ptr<Primitive> spherePrimitive;
    bool useRTGBuffer = true;
    // class Impl;
    // Impl* impl;
};