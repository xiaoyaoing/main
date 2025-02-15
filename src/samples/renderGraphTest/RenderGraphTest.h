//
// Created by pc on 2023/8/17.
//
#pragma once

#include "App/Application.h"
#include "RenderPasses/RenderPassBase.h"

class RenderGraphTest : public Application {
public:
    void prepare() override;

    RenderGraphTest();

protected:
    bool useSubpass{false};

    struct
    {
        std::unique_ptr<PipelineLayout> gBuffer, lighting;
    } pipelineLayouts;

    void                     onUpdateGUI() override;
    void                     drawFrame(RenderGraph& renderGraph) override;
    std::vector<std::unique_ptr<PassBase>> passes;
    
};