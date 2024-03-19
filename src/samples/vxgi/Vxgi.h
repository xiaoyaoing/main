//
// Created by pc on 2023/8/17.
//
#pragma once

#include "ClipmapRegion.h"
#include "ClipmapUpdatePolicy.h"
#include "VxgiCommon.h"
#include "App/Application.h"

class Example : public Application {
public:
    void prepare() override;

    Example();

protected:
    void onUpdateGUI() override;
    void drawFrame(RenderGraph& renderGraph) override;
    void updateClipRegions();
    // void addVoxelizationPass(RenderGraph& rg);
    // void addVoxelConeTracingPass(RenderGraph& rg);
    // void addLightInjectionPass(RenderGraph& rg);

    BBox getBBox(uint32_t clipmapLevel);

    // std::unique_ptr<SgImage> mVoxelizationImage{nullptr};
    // std::unique_ptr<SgImage> normalImage{nullptr};
    // std::unique_ptr<SgImage> depthImage{nullptr};
    // std::unique_ptr<>

    std::vector<std::unique_ptr<VxgiPassBase>> passes{};
    std::vector<BBox>                          mBBoxes{};
    std::vector<ClipmapRegion>                 mClipRegions{};
    std::unique_ptr<ClipmapUpdatePolicy>       mClipmapUpdatePolicy{nullptr};
    inline static uint32_t                     m_clipRegionBBoxExtentL0 = 16;
};