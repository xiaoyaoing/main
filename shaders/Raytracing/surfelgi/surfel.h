#ifndef SURFEL_H
#define SURFEL_H

#include "../commons.h"

struct Surfel {
    vec3 position;
    vec3 normal;
    float radius;
    uint primitive_id;
    uint life;
    float max_inconsistency;
    uint ray_offset;
    uint ray_count;
    uint recycle_time;
};

struct SurfelGridCell {
    uint count;
    uint offset;
};

struct SurfelStats {
    int surfelCount;
    int surfelNextcount;
    int surfelDeadcount;
    int surfelCellallocator;
    int surfelRaycount;
    int surfelShortage;
};

struct SurfelUBO {
    mat4 viewMatrix;             // 视图矩阵
    mat4 projectionMatrix;       // 投影矩阵
    vec3 cameraPosition;         // 相机位置
    float zFar;                  // 远平面距离
    vec2 invResolution;          // 逆分辨率
    float initialRadius;         // 初始surfel半径
    uint maxSurfelPerCell;       // 每个cell的最大surfel数量
    uint maxSurfelCount;         // 最大surfel数量
    uint raysPerSurfel;          // 每个surfel的光线数量
    float zFarRcp;               // 远平面距离的倒数
    float surfelTargetCoverage;  // 目标覆盖率
    float padding[2];            // 对齐填充
};

const int SURFEL_CAPACITY = 20000;

// 网格相关常量
const int SURFEL_GRID_SIZE = 64;
const float SURFEL_CELL_SIZE = 1.0;
const ivec3 surfel_neighbor_offsets[27] = {
    // 生成27个相邻cell的偏移
    ivec3(-1, -1, -1), ivec3(-1, -1, 0), ivec3(-1, -1, 1),
    ivec3(-1,  0, -1), ivec3(-1,  0, 0), ivec3(-1,  0, 1),
    ivec3(-1,  1, -1), ivec3(-1,  1, 0), ivec3(-1,  1, 1),
    ivec3( 0, -1, -1), ivec3( 0, -1, 0), ivec3( 0, -1, 1),
    ivec3( 0,  0, -1), ivec3( 0,  0, 0), ivec3( 0,  0, 1),
    ivec3( 0,  1, -1), ivec3( 0,  1, 0), ivec3( 0,  1, 1),
    ivec3( 1, -1, -1), ivec3( 1, -1, 0), ivec3( 1, -1, 1),
    ivec3( 1,  0, -1), ivec3( 1,  0, 0), ivec3( 1,  0, 1),
    ivec3( 1,  1, -1), ivec3( 1,  1, 0), ivec3( 1,  1, 1)
};

// Dispatch indirect command offsets
const uint BINNING_DISPATCH_OFFSET = 0;
const uint RAYTRACING_DISPATCH_OFFSET = 16;  // sizeof(VkDispatchIndirectCommand)
const uint INTEGRATION_DISPATCH_OFFSET = 32;  // 2 * sizeof(VkDispatchIndirectCommand)
const ivec3 SURFEL_GRID_DIMENSIONS = ivec3(128, 64, 128);
const uint SURFEL_TABLE_SIZE = SURFEL_GRID_DIMENSIONS.x * SURFEL_GRID_DIMENSIONS.y * SURFEL_GRID_DIMENSIONS.z;
const float SURFEL_TARGET_COVERAGE = 0.8f;
// Dispatch indirect command structure (for reference)

// // 辅助函数
// ivec3 surfel_cell(vec3 position) {
//     return ivec3(floor(position / SURFEL_CELL_SIZE));
// }
//
// bool surfel_cellvalid(ivec3 cell) {
//     return all(greaterThanEqual(cell, ivec3(0))) &&
//            all(lessThan(cell, ivec3(SURFEL_GRID_SIZE)));
// }

// uint surfel_cellindex(ivec3 cell) {
//     if (!surfel_cellvalid(cell)) {
//         return 0;
//     }
//     return uint(cell.x + cell.y * SURFEL_GRID_SIZE +
//                cell.z * SURFEL_GRID_SIZE * SURFEL_GRID_SIZE);
// }


const uint SURFEL_MAX_RADIUS = 2;
 const uint SURFEL_INDIRECT_OFFSET_ITERATE = 0;
 const uint SURFEL_INDIRECT_OFFSET_RAYTRACE = SURFEL_INDIRECT_OFFSET_ITERATE + 4 * 3;
 const uint SURFEL_INDIRECT_OFFSET_INTEGRATE = SURFEL_INDIRECT_OFFSET_RAYTRACE + 4 * 3;
 const uint SURFEL_INDIRECT_SIZE = SURFEL_INDIRECT_OFFSET_INTEGRATE + 4 * 3;
const uint SURFEL_INDIRECT_NUMTHREADS = 32;
#endif // SURFEL_H

