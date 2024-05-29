//
// Created by pc on 2023/12/1.
//

#ifndef VULKANDEMO_RAYTRACER_H
#define VULKANDEMO_RAYTRACER_H

#include "App/Application.h"

#include <Core/RayTracing/Accel.h>

#include "Integrators/Integrator.h"
#include "PostProcess/PostProcess.h"
#include "RenderPasses/RenderPassBase.h"

struct RayTracerSettings {};

#define RESTIR_INTEGRATOR_NAME "restir"
#define PATH_INTEGRATOR_NAME   "path"

class RayTracer : public Application {
public:
    RayTracer(const RayTracerSettings& settings);
    void prepare() override;
    void onUpdateGUI() override;
    void drawFrame(RenderGraph& renderGraph) override;
    void onSceneLoaded() override;
    // virtual void update() override;

    // std::unique_ptr<Integrator> path,restirDI{};

    std::unordered_map<std::string_view, std::unique_ptr<Integrator>> integrators;
    std::string_view                                                  currentIntegrator = PATH_INTEGRATOR_NAME;
    std::vector<std::string_view>                                     integratorNames;

    struct
    {
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
    } cameraUbo;

    SceneUbo                      sceneUbo;
    SceneUbo                      lastFrameSceneUbo;
    std::shared_ptr<RTSceneEntry> rtSceneEntry;
    std::unique_ptr<PostProcess>  postProcess;
};

#endif//VULKANDEMO_RAYTRACER_H
