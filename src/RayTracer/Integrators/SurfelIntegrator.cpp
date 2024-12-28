#include"SurfelIntegrator.h"

// 首先定义shader路径
static ShaderKey kGenerateSurfel = "Raytracing/surfel/generate_surfel.comp";
static ShaderKey kUpdateSurfel = "Raytracing/surfel/update_surfel.comp";
static ShaderKey kRayGen = "Raytracing/surfel/raygen.rgen";
static ShaderKey kUpdateAS = "Raytracing/surfel/update_as.comp";
static ShaderKey kIntegrate = "Raytracing/surfel/integrate.comp";
static ShaderKey kFinalGather = "Raytracing/surfel/final_gather.comp";
static ShaderKey kClosestHit = "Raytracing/PT/closesthit.rchit";
static ShaderKey kMiss = "Raytracing/PT/miss.rmiss";

static ShaderPipelineKey RayTracePipeline = {
    kRayGen,
    kMiss,
    kClosestHit
};

void SurfelIntegrator::render(RenderGraph& graph) {
    // 1. Generate Surfels
    graph.addComputePass(
        "Generate Surfels",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            // 声明资源读写
        },
        [&](RenderPassContext& context) {
            // 绑定资源和调度
        });

    // 2. Update Acceleration Structure - Part 1
    graph.addComputePass(
        "Update AS - Cell Count",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        },
        [&](RenderPassContext& context) {
        });

    // 3. Update Acceleration Structure - Part 2
    graph.addComputePass(
        "Update AS - Cell Offset",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        },
        [&](RenderPassContext& context) {
        });

    // 4. Update Acceleration Structure - Part 3
    graph.addComputePass(
        "Update AS - Cell Index",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        },
        [&](RenderPassContext& context) {
        });

    // 5. Ray Generation and Tracing
    graph.addRaytracingPass(
        "Surfel Ray Tracing",
        [&](RenderGraph::Builder& builder, RaytracingPassSettings& settings) {
            settings.shaderPaths = RayTracePipeline;
        },
        [&](RenderPassContext& context) {
        });

    // 6. Integrate Results
    graph.addComputePass(
        "Surfel Integration",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        },
        [&](RenderPassContext& context) {
        });

    // 7. Final Gather
    graph.addComputePass(
        "Final Gather",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
        },
        [&](RenderPassContext& context) {
        });
}

void SurfelIntegrator::init() {
    Integrator::init();
}

void SurfelIntegrator::initScene(RTSceneEntry& entry) {
    Integrator::initScene(entry);
    
    // 创建Surfel相关buffer
    buffers = new SurfelBuffers();
    
    // 创建Surfel数据buffer
    buffers->surfelGeometry = std::make_unique<Buffer>(
        device,
        sizeof(SurfelGeometry) * surfelConfig.max_surfels,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    // 创建双缓冲的累积数据buffer
    for(int i = 0; i < 2; i++) {
        buffers->surfelAccumulation[i] = std::make_unique<Buffer>(
            device,
            sizeof(SurfelAccumulation) * surfelConfig.max_surfels,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
    }
    
    // 创建索引管理buffer
    buffers->aliveBuffer = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * surfelConfig.max_surfels,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    buffers->deadBuffer = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * surfelConfig.max_surfels,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    buffers->aliveCount = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_TO_CPU
    );
    
    buffers->deadCount = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_TO_CPU
    );
    
    // 创建加速结构buffer
    vec3 sceneSize = entry.scene->getSceneBBox().max() - entry.scene->getSceneBBox().min();
    vec3 cellCounts = ceil(sceneSize / surfelConfig.cell_size);
    uint32_t totalCells = cellCounts.x * cellCounts.y * cellCounts.z;
    
    buffers->cellCounts = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * totalCells,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    buffers->cellOffsets = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * totalCells,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    buffers->cellSurfelIndices = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * surfelConfig.max_surfels * 8, // 假设每个surfel最多影响8个cell
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    // // 创建ray tracing相关buffer
    // buffers->rayBuffer = std::make_unique<Buffer>(
    //     device,
    //     sizeof(Ray) * surfelConfig.max_surfels * surfelConfig.rays_per_surfel,
    //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    //     VMA_MEMORY_USAGE_GPU_ONLY
    // );
    
    buffers->rayOffsets = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * surfelConfig.max_surfels,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    buffers->rayCounts = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * surfelConfig.max_surfels,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    
    // 初始化dead buffer为所有可用索引
    std::vector<uint32_t> deadIndices(surfelConfig.max_surfels);
    for(uint32_t i = 0; i < surfelConfig.max_surfels; i++) {
        deadIndices[i] = i;
    }
    buffers->deadBuffer->uploadData(deadIndices.data(), deadIndices.size() * sizeof(uint32_t));
    
    // 初始化计数器
    uint32_t zero = 0;
    uint32_t maxSurfels = surfelConfig.max_surfels;
    buffers->aliveCount->uploadData(&zero, sizeof(uint32_t));
    buffers->deadCount->uploadData(&maxSurfels, sizeof(uint32_t));
}

SurfelIntegrator::SurfelIntegrator(Device& device, DDGIConfig config):Integrator(device) {
    // 初始化配置
}

void SurfelIntegrator::onUpdateGUI() {
    Integrator::onUpdateGUI();
    // 添加Surfel相关的GUI控制
}
