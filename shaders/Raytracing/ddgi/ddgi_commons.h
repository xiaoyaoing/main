#ifndef DDGI_COMMONS_H
#define DDGI_COMMONS_H

#include "../commons.h"

struct DDGIUbo {
    ivec3 probe_counts;
    float hysteresis;
    
    vec3  probe_start_position;
    float pad;
    
    vec3  probe_distance;
    float pad2;

    int   rays_per_probe;
    float max_distance;
    float depth_sharpness;
    int   irradiance_width;
    
    int   irradiance_height;
    int   depth_width;
    int   depth_height;
    float min_frontface_distance;
};

struct DDGIRayData {
    vec3  direction;
    float pad;
    vec3  irradiance;
    float dist;
};

// struct DDGIGBuffer {
//     vec3 position;
//     uint material_idx;
//     vec2 uv;
//     vec2 pad;
//     vec3 normal;
//     uint pad2;
// };

// static const uint DDGI_COLOR_RESOLUTION = 6; // this should not be modified, border update code is fixed
// static const uint DDGI_COLOR_TEXELS = 1 + DDGI_COLOR_RESOLUTION + 1; // with border. NOTE: this must be 4x4 block aligned for BC6!
// static const uint DDGI_DEPTH_RESOLUTION = 16; // this should not be modified, border update code is fixed
// static const uint DDGI_DEPTH_TEXELS = 1 + DDGI_DEPTH_RESOLUTION + 1;

#endif