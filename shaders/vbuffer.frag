#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "perFrame.glsl"
#include "perFrameShading.glsl"

precision highp float;

layout (location = 0) in vec2 in_uv;
layout (location = 1) in vec3 in_normal;
layout (location = 2) flat in uint in_primitive_index;
layout (location = 3) in vec3 in_world_pos;

// VBuffer输出
layout (location = 0) out uint o_primitive_id;
layout (location = 1) out vec4 o_normal;
layout (location = 2) out vec2 o_uv;

layout(std430, set = 0, binding = 2) buffer _GlobalPrimitiveUniform {
    PerPrimitive primitive_infos[];
};

vec3 getNormal(const int texture_idx)
{
    if (texture_idx < 0){
        return normalize(in_normal);
    }
    vec3 tangentNormal = texture(scene_textures[texture_idx], in_uv).xyz * 2.0 - 1.0;

    vec3 q1 = dFdx(in_world_pos);
    vec3 q2 = dFdy(in_world_pos);
    vec2 st1 = dFdx(in_uv);
    vec2 st2 = dFdy(in_uv);

    vec3 N = normalize(in_normal);
    vec3 T = normalize(q1 * st2.t - q2 * st1.t);
    vec3 B = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

void main(void)
{
    uint material_index = primitive_infos[in_primitive_index].material_index;
    GltfMaterial material = scene_materials[material_index];

    // 输出primitive id
    o_primitive_id = in_primitive_index;

    // 计算并输出normal
    vec3 normal = getNormal(material.normalTexture);
    o_normal = vec4(normal * 0.5f + 0.5f, 0.0);

    // 输出uv
    o_uv = in_uv;
}
