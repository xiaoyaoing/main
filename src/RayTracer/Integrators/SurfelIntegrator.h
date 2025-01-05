#pragma once
#include "Integrator.h"
#include "Common/RenderConfig.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderPasses/RasterizationPass.h"

// Surfel几何结构
// struct SurfelGeometry {
//     vec3 local_position;
//     vec3 local_normal;
//     int primitiveId;
//     float radius;
// };
//
// // Surfel累积数据
// struct SurfelAccumulation {
//     vec3 short_mean;    // 短期平均辐照度
//     vec3 mean;          // 长期平均辐照度
//     float confidence;   // 收敛程度
// };

// Surfel完整数据结构
// struct Surfel {
//     SurfelGeometry geometry;
//     SurfelAccumulation accumulation;
// };
//


class SurfelIntegrator : public Integrator {
    void render(RenderGraph& graph) override;

public:
    void init() override;
    void initScene(RTSceneEntry& entry) override;
    SurfelIntegrator(Device& device,SurfelConfig config);
    void onUpdateGUI() override;

protected:
    struct SurfelBuffers {
        // Surfel数据
        std::unique_ptr<Buffer> surfels;
        std::unique_ptr<Buffer> surfelUBOBuffer;  // 新增Surfel UBO Buffer

        // 索引管理
        std::unique_ptr<Buffer> aliveBuffer;        // 活跃的surfel索引
        std::unique_ptr<Buffer> deadBuffer;         // 已回收的surfel索引
        std::unique_ptr<Buffer> surfelStats;		// Surfel统计信息

        // 加速结构
        std::unique_ptr<Buffer> surfelCell;
        std::unique_ptr<Buffer> gridBuffer;

        // Ray tracing相关
        std::unique_ptr<Buffer> rayBuffer;          // 光线数据
        std::unique_ptr<Buffer> rayOffsets;         // 每个surfel的ray起始偏移
        std::unique_ptr<Buffer> rayCounts;          // 每个surfel的ray数量

    };

    struct SurfelVisConfig {
        int enabled = 0;            // 是否启用可视化
        float surfelSize = 0.1f;    // surfel显示大小
        int showIrradiance = 1;     // 是否显示辐照度
        float padding;              // 对齐
    } visConfig;


    std::unique_ptr<Buffer> surfelIndirectBuffer{nullptr};
    std::unique_ptr<VBufferPass> vBufferPass;
    SurfelBuffers* buffers{nullptr};
    SurfelConfig surfelConfig;
};
