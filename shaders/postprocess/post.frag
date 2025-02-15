#version 450
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_atomic_float : require

#include "tonemap.glsl"

struct PCPost {
    uint tone_mapper;
    uint enable_bloom;
    int width;
    int height;
    float bloom_exposure;
    float bloom_amount;
    int gamma_correction;
    int dither;
    int frame_number;
};

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 fragColor;

layout(set = 1, binding = 0) uniform  sampler2D input_img;
layout(set = 1, binding = 1) uniform  sampler2D blue_noise_tex;
layout(push_constant) uniform PCPost_ { PCPost pc; };

const float goldenRatioConjugate = 0.61803398875;

vec3 GetLowDiscrepancyBlueNoise(ivec2 screenPosition, uint frameNumber, float noiseScale, sampler2D blueNoise) {
    // Load random value from a blue noise texture
    vec3 rnd = texture(blueNoise, vec2(screenPosition % 256 + vec2(0.5f)) / 256.0).rgb;

    // Generate a low discrepancy sequence
    rnd = fract(rnd + goldenRatioConjugate * float((frameNumber - 1) % 16));

    // Scale the noise magnitude to [0, noiseScale]
    return rnd * noiseScale;
}

vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float aces(float x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

//From 
vec3 Tonemap(vec3 color, float exposure, uint tonemapper)
{
    color  *= exposure;

    switch (tonemapper)
    {
        case 0: return AMDTonemapper(color);
        case 1: return DX11DSK(color);
        case 2: return Reinhard(color);
        case 3: return Uncharted2Tonemap(color);
        case 4: return ACESFilm(color);
        case 5: return color;
        default : return vec3(1, 1, 1);
    }
}




void main() {
    vec4 input_tex = texture(input_img, vec2(in_uv.x,  in_uv.y));
    vec4 img;
    if (pc.enable_bloom == 1) {
        //        vec4 bloom_tex = texelFetch(bloom_img, (textureSize(bloom_img, 0).xy - ivec2(pc.width, pc.height)) / 2 + ivec2(gl_FragCoord.xy), 0);
        //        img = mix(input_tex, bloom_tex * pc.bloom_exposure, pc.bloom_amount);
    } else {
        img = input_tex;
    }
    img = vec4(Tonemap(img.xyz, pc.bloom_exposure, pc.tone_mapper), 1.0);
    fragColor =  img;
    
    if(pc.dither > 0) {
        vec3 dither = GetLowDiscrepancyBlueNoise(ivec2(gl_FragCoord.xy), pc.frame_number, 1.f / 255.f, blue_noise_tex);
        fragColor.rgb += dither;
    }
    if(pc.gamma_correction > 0) {
        fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / 2.2));
    }
}
