#pragma once

#include "Integrator.h"
#include "Common/RenderConfig.h"
#include "Raytracing/PT/path_commons.h"
#include "RenderPasses/RasterizationPass.h"

class PathIntegrator : public Integrator {
public:
    void render(RenderGraph& renderGraph) override;
    void initScene(RTSceneEntry & entry) override;
    void onUpdateGUI() override;
    bool resetFrameOnCameraMove() const override;
    PathIntegrator(Device& device,PathTracingConfig config);

    // ~PathIntegrator();
protected:
    GBufferPass                     gbufferPass;
    LightingPass                    lightingPass;
    std::unique_ptr<PipelineLayout> layout;
   struct
    {
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
    } cameraUbo;
    PathTracingConfig config;
};
