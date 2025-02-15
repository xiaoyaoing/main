#pragma once
#include "RenderPasses/RenderPassBase.h"

class Sampler;
class PipelineLayout;
class PostProcess : public PassBase {
public:
    struct PCPost {
        uint32_t tone_mapper{2};
        uint32_t enable_bloom{0};
        int      width;
        int      height;
        float    bloom_exposure{1};
        float    bloom_amount;
        int gamma_correction{0};
        int dither{0};
        int frame_number{0};
    };

    void init() override;
    void render(RenderGraph& rg) override;
   // void render(RenderGraph& rg,RenderGraphHandle 
    void updateGui() override;

protected:
    const PipelineLayout*    mPipelineLayout{nullptr};
    const Sampler*           mSampler{nullptr};
    PCPost                   pcPost{};
    std::vector<const char*> tonemapperNames;
};
