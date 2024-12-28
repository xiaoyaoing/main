#pragma once
#include "Integrator.h"
#include "Common/RenderConfig.h"
#include "Raytracing/PT/path_commons.h"
#include "Raytracing/ddgi/ddgi_commons.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderPasses/GBufferPass.h"
#include "RenderPasses/ShadowMapPass.h"

#include <variant>

// Surfel几何结构
struct SurfelGeometry {
    vec3 local_position;
    vec3 local_normal;
    int primitiveId;
    float radius;
};

// Surfel累积数据
struct SurfelAccumulation {
    vec3 short_mean;    // 短期平均辐照度
    vec3 mean;          // 长期平均辐照度
    float confidence;   // 收敛程度
};

// Surfel完整数据结构
struct Surfel {
    SurfelGeometry geometry;
    SurfelAccumulation accumulation;
};



class SurfelIntegrator : public Integrator {
    void render(RenderGraph& graph) override;

public:
    void init() override;
    void initScene(RTSceneEntry& entry) override;
    SurfelIntegrator(Device& device,DDGIConfig config);
    void onUpdateGUI() override;

protected:
    struct SurfelBuffers {
        // Surfel数据
        std::unique_ptr<Buffer> surfelGeometry;     // 几何数据
        std::unique_ptr<Buffer> surfelAccumulation[2]; // 累积数据(双缓冲)
        
        // 索引管理
        std::unique_ptr<Buffer> aliveBuffer;        // 活跃的surfel索引
        std::unique_ptr<Buffer> deadBuffer;         // 已回收的surfel索引
        std::unique_ptr<Buffer> aliveCount;         // 活跃surfel计数
        std::unique_ptr<Buffer> deadCount;          // 死亡surfel计数
        
        // 加速结构
        std::unique_ptr<Buffer> cellCounts;         // 每个cell中的surfel数量
        std::unique_ptr<Buffer> cellOffsets;        // 每个cell的起始偏移
        std::unique_ptr<Buffer> cellSurfelIndices;  // cell中的surfel索引列表
        
        // Ray tracing相关
        std::unique_ptr<Buffer> rayBuffer;          // 光线数据
        std::unique_ptr<Buffer> rayOffsets;         // 每个surfel的ray起始偏移
        std::unique_ptr<Buffer> rayCounts;          // 每个surfel的ray数量
    };
    SurfelBuffers* buffers{nullptr};
    SurfelConfig surfelConfig;
};