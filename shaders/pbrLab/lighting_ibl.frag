#version 460 core

#extension GL_GOOGLE_include_directive : enable 
#extension GL_EXT_debug_printf : enable


#include "../perFrame.glsl"
//#include "../shadow.glsl"
#include "../lighting.glsl"
#include "../brdf.glsl"

precision highp float;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;


#define ROUGHNESS_PREFILTER_COUNT 5

layout(input_attachment_index = 0, binding = 0, set=2) uniform subpassInput gbuffer_diffuse_roughness;
//layout(input_attachment_index = 1, binding = 1, set=2) uniform subpassInput gbuffer_specular;
layout(input_attachment_index = 1, binding = 1, set=2) uniform subpassInput gbuffer_normal_metalic;
layout(input_attachment_index = 2, binding = 2, set=2) uniform subpassInput gbuffer_emission;
layout(input_attachment_index = 3, binding = 3, set=2) uniform subpassInput gbuffer_depth;

layout(binding = 0, set=1) uniform sampler2D brdf_lut;
layout(binding = 1, set=1) uniform samplerCube irradiance_map;
layout(binding = 2, set=1) uniform samplerCube prefilter_map;

layout(push_constant) uniform Params
{
    float exposure;
    float gamma;
    float scaleIBLAmbient;
    float prefilteredCubeMipLevels;
    int debugMode;
    int padding[3];
};

vec3 Uncharted2Tonemap(vec3 color)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}


vec4 tonemap(vec4 color)
{
    vec3 outcol = Uncharted2Tonemap(color.rgb * exposure);
    outcol = outcol * (1.0f / Uncharted2Tonemap(vec3(11.2f)));
    return vec4(pow(outcol, vec3(1.0f / gamma)), color.a);
}

vec4 SRGBtoLINEAR(vec4 srgbIn)
{
    vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
    vec3 linOut = mix(srgbIn.xyz/vec3(12.92), pow((srgbIn.xyz+vec3(0.055))/vec3(1.055), vec3(2.4)), bLess);
    return vec4(linOut, srgbIn.w);;
}

vec3 ibl_fragment_shader(const in PBRInfo pbr_info, vec3 n, vec3 reflection)
{
    // return vec3(1.0);
    //  return vec3(pbr_info.alphaRoughness);

    float lod = (pbr_info.perceptualRoughness * prefilteredCubeMipLevels);

    // retrieve a scale and bias to F0. See [1], Figure 3
    //  debugPrintfEXT("lod: NdotV roughness %f %f %f\n", lod, pbr_info.NdotV, pbr_info.alphaRoughness);

    vec3 brdf = (texture(brdf_lut, vec2(pbr_info.NdotV, 1.0 - pbr_info.perceptualRoughness))).rgb;


    vec3 diffuseLight = SRGBtoLINEAR(tonemap(texture(irradiance_map, n))).rgb;

    vec3 specularLight = SRGBtoLINEAR(tonemap(textureLod(prefilter_map, reflection, lod))).rgb;

    vec3 diffuse = diffuseLight * pbr_info.diffuseColor;
    vec3 specular = specularLight * (pbr_info.F0 * brdf.x + brdf.y);
    specular =  specularLight;

    // For presentation, this allows us to disable IBL terms
    // For presentation, this allows us to disable IBL terms
    diffuse *= scaleIBLAmbient;
    specular *= scaleIBLAmbient;

    if (debugMode == 1)
    {
        return diffuse;
    }
    else if (debugMode == 2)
    {
        return specular;
    }
    return diffuse + specular;
}


void main(){
    return;
    vec4  diffuse_roughness  = subpassLoad(gbuffer_diffuse_roughness);
    vec4  normal_metalic    = subpassLoad(gbuffer_normal_metalic);

    vec3 normal      = normalize(2.0 * normal_metalic.xyz - 1.0);
    float metallic    = normal_metalic.w;

    vec3  emission = subpassLoad(gbuffer_emission).rgb;
    float depth    = subpassLoad(gbuffer_depth).x;

    if (depth == 0.0){
        discard;
    }

    vec3 world_pos = worldPosFromDepth(in_uv, depth);


    vec3 diffuse_color = diffuse_roughness.xyz;
    float perceptual_roughness = diffuse_roughness.a;

    vec3 view_dir = per_frame.camera_pos - world_pos;


    //calcuate sppecular contribution
    vec3 color = vec3(0.0);


    bool has_emission = any(greaterThan(emission, vec3(1e-6)));
    if (has_emission)
    {
        color+= emission;
    }

    {
        // calculate Microfacet BRDF model
        // Roughness is authored as perceptual roughness; as is convention
        // convert to material roughness by squaring the perceptual roughness [2].
        // for 

        vec3 view_dir = normalize(per_frame.camera_pos - world_pos);
        vec3 R = 2 * dot(normal, view_dir) * normal - view_dir;

        PBRInfo pbr_info;
        // why use abs here?
        pbr_info.NdotV = clamp(abs(dot(normal, view_dir)), 0.001, 1.0);

        pbr_info.F0 = mix(vec3(0.04), diffuse_color, metallic);
        pbr_info.F90 = vec3(1.0);
        pbr_info.alphaRoughness = perceptual_roughness * perceptual_roughness;
        pbr_info.perceptualRoughness = perceptual_roughness;
        //  pbr_info.alphaRoughness = 0.01f;
        pbr_info.diffuseColor = diffuse_color;

        color += ibl_fragment_shader(pbr_info, normal, R);
    }
    out_color = vec4(color, 1);
}