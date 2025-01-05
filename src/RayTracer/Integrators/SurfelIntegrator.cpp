#include"SurfelIntegrator.h"

#include "imgui.h"
#include "Core/View.h"
#include "shaders/Raytracing/surfelgi/surfel.h"
// 首先定义shader路径
static ShaderKey kGenerateSurfel = "Raytracing/surfelgi/generate_surfel.comp";
static ShaderKey kUpdateSurfel   = "Raytracing/surfelgi/update_surfel.comp";
static ShaderKey kRayGen         = "Raytracing/surfelgi/raygen.rgen";
static ShaderKey kUpdateAS       = "Raytracing/surfelgi/update_as.comp";
static ShaderKey kIntegrate      = "Raytracing/surfelgi/integrate.comp";
static ShaderKey kFinalGather    = "Raytracing/surfelgi/final_gather.comp";
static ShaderKey kClosestHit     = "Raytracing/PT/closesthit.rchit";
static ShaderKey kMiss           = "Raytracing/PT/miss.rmiss";
static ShaderKey kSurfelVis      = "Raytracing/surfelgi/surfel_visualization.comp";
static ShaderKey kPrimaryRayVBuffer = {"Raytracing/primary_ray.rgen", {"ENABLE_VBUFFER"}};
static ShaderKey kMissShadow         = "Raytracing/PT/miss_shadow.rmiss";
static ShaderKey kRayAnyHit          = "Raytracing/ray.rahit";

static ShaderPipelineKey RayTracePipeline = {
    kRayGen,
    kMiss,
    kClosestHit
};

static ShaderPipelineKey PrimaryRayGen = {
     kPrimaryRayVBuffer,
    kMiss,
    kMissShadow,
    kClosestHit,
    kRayAnyHit};

void SurfelIntegrator::render(RenderGraph& graph) {
    if (!entry_->primAreaBuffersInitialized) { initLightAreaDistribution(graph); }

    graph.setCutUnUsedResources(false);
    graph.addRaytracingPass(
                "Generate Primary Rays",
                [&](RenderGraph::Builder& builder, RaytracingPassSettings& settings) {
                    settings.accel                   = &entry_->tlas;
                    settings.shaderPaths             = PrimaryRayGen;
                    settings.rTPipelineSettings.dims = {width, height, 1};
                    builder.writeTexture(RT_IMAGE_NAME, TextureUsage::STORAGE);
                    auto vbuffer = graph.createTexture(
						VBUFFER_RG,
						{.extent = {width, height},
						    .useage = TextureUsage::STORAGE | TextureUsage::SAMPLEABLE,
						 .format = VK_FORMAT_R32G32B32A32_SFLOAT
						 });
                    builder.writeTexture(vbuffer, TextureUsage::STORAGE);
                    settings.rTPipelineSettings.maxDepth = 2;
                },
                [&](RenderPassContext& context) {
                    auto& commandBuffer = context.commandBuffer;
                    bindRaytracingResources(commandBuffer);
                    g_context->bindImage(0, graph.getBlackBoard().getImageView(RT_IMAGE_NAME));
                    g_context->bindImage(2, graph.getBlackBoard().getImageView(VBUFFER_RG));
                    g_context->bindPushConstants(getPC());
                    g_context->traceRay(commandBuffer, VkExtent3D{width, height, 1});
                });

    // 1. Generate Surfels (直接dispatch)
    graph.addComputePass(
        "Generate Surfels",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            auto surfelStats   = graph.importBuffer("surfel_stats", buffers->surfelStats.get());
            auto surfelBuffer  = graph.importBuffer("surfel", buffers->surfels.get());
            auto aliveBuffer   = graph.importBuffer("surfel_alive", buffers->aliveBuffer.get());
            auto deadBuffer    = graph.importBuffer("surfel_dead", buffers->deadBuffer.get());
            auto cellBuffer    = graph.importBuffer("surfel_cell", buffers->surfelCell.get());
            auto gridBuffer    = graph.importBuffer("surfel_grid", buffers->gridBuffer.get());

            builder.writeBuffer(surfelStats, BufferUsage::STORAGE)
                   .writeBuffer(surfelBuffer, BufferUsage::STORAGE)
                   .writeBuffer(aliveBuffer, BufferUsage::STORAGE)
                   .writeBuffer(deadBuffer, BufferUsage::STORAGE)
                   .writeBuffer(cellBuffer, BufferUsage::STORAGE)
                   .writeBuffer(gridBuffer, BufferUsage::STORAGE);

            auto vbuffer = graph.getBlackBoard().getHandle(VBUFFER_RG);
            builder.readTexture(vbuffer, TextureUsage::SAMPLEABLE);
        },
        [&](RenderPassContext& context) {
            g_context->bindBuffer(1, *buffers->surfelStats)   // 对应set = 0, binding = 1
                     .bindBuffer(2, *buffers->surfels)        // 对应set = 0, binding = 2
                     .bindBuffer(3, *buffers->aliveBuffer)    // 对应set = 0, binding = 3
                     .bindBuffer(4, *buffers->deadBuffer)     // 对应set = 0, binding = 4
                     .bindBuffer(5, *buffers->surfelCell)     // 对应set = 0, binding = 5
                     .bindBuffer(6, *buffers->gridBuffer);    // 对应set = 0, binding = 6
            g_manager->getView()->bindViewBuffer();
            g_context->bindShaders({kGenerateSurfel})
                     .flushAndDispatch(context.commandBuffer, (width + 15) / 16, (height + 15) / 16, 1);
        });

    return;

    graph.addComputePass("prepare indirect buffer",
                         [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
                             auto surfelIndirectBufferHandle = graph.importBuffer("surfel_indirect", surfelIndirectBuffer.get());
                             builder.writeBuffer(surfelIndirectBufferHandle, BufferUsage::INDIRECT);
                         },
                         [&](RenderPassContext& context) {
                             g_context->bindShaders({kGenerateSurfel})
                                      .flushAndDispatch(context.commandBuffer, 1, 1, 1);
                         });

    // 添加Surfel Binning Pass
    graph.addComputePass(
        "Surfel Binning",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            auto surfelBuffer = graph.importBuffer("surfel", buffers->surfels.get());
            auto aliveBuffer  = graph.importBuffer("surfel_alive", buffers->aliveBuffer.get());
            auto gridBuffer   = graph.importBuffer("surfel_grid", buffers->gridBuffer.get());
            auto cellBuffer   = graph.importBuffer("surfel_cell", buffers->surfelCell.get());

            builder.readBuffer(surfelBuffer, BufferUsage::STORAGE)
                   .readBuffer(aliveBuffer, BufferUsage::STORAGE)
                   .writeBuffer(gridBuffer, BufferUsage::STORAGE)
                   .writeBuffer(cellBuffer, BufferUsage::STORAGE);
        },
        [&](RenderPassContext& context) {
            g_manager->getView()->bindViewBuffer();
            g_context->bindBuffer(1, *buffers->surfels)
                     .bindBuffer(2, *buffers->aliveBuffer)
                     .bindBuffer(3, *buffers->gridBuffer)
                     .bindBuffer(4, *buffers->surfelCell);

            g_context->bindShaders({"Raytracing/surfelgi/surfel_binning.comp"})
                     .flushAndDispatchIndirect(context.commandBuffer, *surfelIndirectBuffer, BINNING_DISPATCH_OFFSET);
        });

    return;

    // 2. Binning Surfels (使用indirect dispatch)
    graph.addComputePass(
        "Surfel Binning",
        [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
            // auto surfelGrid = graph.importBuffer("surfel_grid", buffers->cellCounts.get());
            //   auto surfelCell = graph.importBuffer("surfel_cell", buffers->cellSurfelIndices.get());

            // builder.writeBuffer(surfelGrid, BufferUsage::STORAGE)
            //       .writeBuffer(surfelCell, BufferUsage::STORAGE);
        },
        [&](RenderPassContext& context) {
            g_context->bindBuffer(0, *buffers->surfels)
                     .bindBuffer(1, *buffers->aliveBuffer);
            // .bindBuffer(2, *buffers->cellCounts)
            // .bindBuffer(3, *buffers->cellSurfelIndices);

            g_context->bindShaders({kUpdateAS})
                     .bindPushConstants(getPC())
                     .flushAndDispatchIndirect(context.commandBuffer,
                                               *surfelIndirectBuffer,
                                               SURFEL_INDIRECT_OFFSET_ITERATE);
        });

    // 3. Ray Tracing Pass (使用indirect dispatch)
    // graph.addRaytracingPass(
    //     "Surfel Ray Tracing",
    //     [&](RenderGraph::Builder& builder, RaytracingPassSettings& settings) {
    //         settings.accel = &entry_->tlas;
    //         settings.shaderPaths = RayTracePipeline;
    //         settings.rTPipelineSettings.dims = {width, height, 1};
    //         settings.rTPipelineSettings.maxDepth = 5;
    //
    //         auto rayBuffer = graph.importBuffer("surfel_rays", buffers->rayBuffer.get());
    //         builder.writeBuffer(rayBuffer, BufferUsage::STORAGE);
    //     },
    //     [&](RenderPassContext& context) {
    //         bindRaytracingResources(context.commandBuffer);
    //         g_context->bindBuffer(0, *buffers->surfelGeometry)
    //                  .bindBuffer(1, *buffers->rayBuffer)
    //                  .bindBuffer(2, *buffers->rayOffsets)
    //                  .bindBuffer(3, *surfelIndirectBuffer);
    //
    //         g_context->bindPushConstants(getPC())
    //                  .traceRayIndirect(context.commandBuffer,
    //                                  *surfelIndirectBuffer,
    //                                  buffers->rayTracingDispatchOffset);
    //     });

    // 添加Surfel可视化pass
    if (visConfig.enabled) {
        graph.addComputePass(
            "Surfel Visualization",
            [&](RenderGraph::Builder& builder, ComputePassSettings& settings) {
                auto surfelBuffer = graph.importBuffer("surfel", buffers->surfels.get());
                builder.readBuffer(surfelBuffer, BufferUsage::STORAGE);
            },
            [&](RenderPassContext& context) {
                g_context->bindBuffer(0, *buffers->surfels);
                g_context->bindShaders({
                    "Raytracing/surfelgi/debug/surfel_vis.mesh",
                    "Raytracing/surfelgi/debug/surfel_vis.task",
                    "Raytracing/surfelgi/debug/surfel_vis.frag"
                });
                g_context->bindPushConstants(getPC());
                g_context->flushAndDrawMeshTasksIndirect(context.commandBuffer, *surfelIndirectBuffer, SURFEL_INDIRECT_OFFSET_ITERATE);
            });
    }

    // 更新Surfel UBO
    // SurfelUBO updatedUBO = {};
    // updatedUBO.viewMatrix = ...;  // 更新视图矩阵
    // updatedUBO.projectionMatrix = ...;  // 更新投影矩阵
    // updatedUBO.cameraPosition = ...;  // 更新相机位置
    // updatedUBO.zFar = ...;  // 更新远平面距离
    // updatedUBO.invResolution = ...;  // 更新逆分辨率
    // updatedUBO.initialRadius = ...;  // 更新surfel半径
    // updatedUBO.maxSurfelPerCell = ...;  // 更新每个cell的最大surfel数量
    // updatedUBO.maxSurfelCount = ...;  // 更新最大surfel数量
    // updatedUBO.raysPerSurfel = ...;  // 更新每个surfel的光线数量
    // updatedUBO.zFarRcp = 1.0f / updatedUBO.zFar;  // 计算远平面距离的倒数
    // updatedUBO.surfelTargetCoverage = ...;  // 更新目标覆盖率
    //
    // buffers->surfelUBOBuffer->uploadData(&updatedUBO, sizeof(SurfelUBO));
}

void SurfelIntegrator::init() {
    Integrator::init();
    vBufferPass = std::make_unique<VBufferPass>();
}

void SurfelIntegrator::initScene(RTSceneEntry& entry) {
    Integrator::initScene(entry);

    // 创建Surfel相关buffer
    buffers = new SurfelBuffers();

    // 创建Surfel数据buffer
    buffers->surfels = std::make_unique<Buffer>(
        device,
        sizeof(Surfel) * SURFEL_CAPACITY,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
        );

    // 创建索引管理buffer
    buffers->aliveBuffer = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * SURFEL_CAPACITY,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
        );

    std::vector<uint32_t> deadIndices(SURFEL_CAPACITY);
    for (uint32_t i = 0; i < SURFEL_CAPACITY; i++) { deadIndices[i] = i; }

    buffers->deadBuffer = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * SURFEL_CAPACITY,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        deadIndices.data()
        );

    SurfelStats stats{0, 0, 0, SURFEL_CAPACITY, 0, 0};

    buffers->surfelStats = std::make_unique<Buffer>(device,
                                                    sizeof(SurfelStats),
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                    VMA_MEMORY_USAGE_GPU_TO_CPU,
                                                    &stats
    );

    // 创建加速结构buffer
    // vec3 sceneSize = entry.scene->getSceneBBox().max() - entry.scene->getSceneBBox().min();
    // vec3 cellCounts = ceil(sceneSize / surfelConfig.cell_size);
    // uint32_t totalCells = cellCounts.x * cellCounts.y * cellCounts.z;


    std::vector<uint32_t> cellIndices(SURFEL_CAPACITY * 27, 0);

    buffers->surfelCell = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * SURFEL_CAPACITY * 27,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,cellIndices.data()
        );
    std::vector<SurfelGridCell> gridCells(SURFEL_TABLE_SIZE,{0,0});

    buffers->gridBuffer = std::make_unique<Buffer>(
        device,
        sizeof(SurfelGridCell) * SURFEL_TABLE_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,gridCells.data()
        );

    // // 创建ray tracing相关buffer
    // buffers->rayBuffer = std::make_unique<Buffer>(
    //     device,
    //     sizeof(Ray) * SURFEL_CAPACITY * surfelConfig.rays_per_surfel,
    //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    //     VMA_MEMORY_USAGE_GPU_ONLY
    // );


    buffers->rayOffsets = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * SURFEL_CAPACITY,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
        );

    buffers->rayCounts = std::make_unique<Buffer>(
        device,
        sizeof(uint32_t) * SURFEL_CAPACITY,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
        );

    // 初始化dead buffer为所有可用索引

    //buffers->deadBuffer->uploadData(deadIndices.data(), deadIndices.size() * sizeof(uint32_t));

    // 初始化计数器
    uint32_t zero       = 0;
    uint32_t maxSurfels = SURFEL_CAPACITY;
    // buffers->aliveCount->uploadData(&zero, sizeof(uint32_t));
    // buffers->deadCount->uploadData(&maxSurfels, sizeof(uint32_t));

    // 初始化indirect dispatch buffer
    surfelIndirectBuffer = std::make_unique<Buffer>(
        device,
        SURFEL_INDIRECT_SIZE,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
        );

    // 初始化dispatch buffer
    std::vector<uint32_t> dispatchData = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    surfelIndirectBuffer->uploadData(dispatchData.data(), dispatchData.size() * sizeof(uint32_t));

    // 创建Surfel UBO Buffer
    buffers->surfelUBOBuffer = std::make_unique<Buffer>(
        device,
        sizeof(SurfelUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    // 初始化Surfel UBO
    // SurfelUBO initialUBO = {};
    // initialUBO.viewMatrix = ...;  // 初始化视图矩阵
    // initialUBO.projectionMatrix = ...;  // 初始化投影矩阵
    // initialUBO.cameraPosition = ...;  // 初始化相机位置
    // initialUBO.zFar = ...;  // 初始化远平面距离
    // initialUBO.invResolution = ...;  // 初始化逆分辨率
    // initialUBO.initialRadius = ...;  // 初始化surfel半径
    // initialUBO.maxSurfelPerCell = ...;  // 初始化每个cell的最大surfel数量
    // initialUBO.maxSurfelCount = ...;  // 初始化最大surfel数量
    // initialUBO.raysPerSurfel = ...;  // 初始化每个surfel的光线数量
    // initialUBO.zFarRcp = 1.0f / initialUBO.zFar;  // 计算远平面距离的倒数
    // initialUBO.surfelTargetCoverage = ...;  // 初始化目标覆盖率
    //
    // buffers->surfelUBOBuffer->uploadData(&initialUBO, sizeof(SurfelUBO));
}

SurfelIntegrator::SurfelIntegrator(Device& device, SurfelConfig config): Integrator(device), surfelConfig(config) {
    // 初始化配置
}

void SurfelIntegrator::onUpdateGUI() {
    Integrator::onUpdateGUI();

    if (ImGui::CollapsingHeader("Surfel Visualization")) {
        ImGui::Checkbox("Enable Visualization", (bool*)&visConfig.enabled);
        if (visConfig.enabled) {
            ImGui::SliderFloat("Surfel Size", &visConfig.surfelSize, 0.001f, 1.0f);
            ImGui::Checkbox("Show Irradiance", (bool*)&visConfig.showIrradiance);
        }
    }
}
