#define PROBE_DEPTH_WITH_BORDER_SIDE 18
#define PROBE_DEPTH_SIDE 16
#define PROBE_RADIANCE_WITH_BORDER_SIDE 10
#define PROBE_RADIANCE_SIDE 8

ivec3 get_probe_coord_by_index(int probe_idx){
    ivec3 probe_counts  = ddgi_ubo.probe_counts;
    return ivec3(probe_idx % probe_counts.x, (probe_idx % (probe_counts.x * probe_counts.y) ) / probe_counts.x, probe_idx / (probe_counts.x * probe_counts.y));
}

int get_probe_index_by_coord(ivec3 probe_coord){
    ivec3 probe_counts = ddgi_ubo.probe_counts;
    return probe_coord.x + probe_coord.y * probe_counts.x + probe_coord.z * probe_counts.x * probe_counts.y;
}



ivec3 get_probe_coord_by_position(vec3 position){
    vec3 probe_grid_origin = ddgi_ubo.probe_start_position;
    vec3 probe_grid_size = ddgi_ubo.probe_distance;
    vec3 probe_grid = (position - probe_grid_origin) / probe_grid_size;
    return ivec3(probe_grid);
}

vec3 get_surface_bias(vec3 normal,vec3 view,float normal_bias,float view_bias){
    return normal * normal_bias - view * view_bias;
}

vec3 get_position_by_grid(ivec3 grid){
    vec3  probe_grid_size = ddgi_ubo.probe_distance;
    vec3 probe_grid_origin =ddgi_ubo.probe_start_position;
    return probe_grid_origin + vec3(grid) * probe_grid_size;
}

//vec3 probe_location(int probe_idx){
//    return get_position_by_grid(get_probe_coord_by_index(probe_idx));
//}

ivec2 get_pixel_coord_top_left(ivec3 probe_coord,int border_side){
    return probe_coord.xz * border_side  + ivec2(probe_coord.y * ddgi_ubo.probe_counts.x * border_side, 0);
}


vec2 normalized_oct_coord(ivec2 oct_coord,int side_length) {
    // Local oct texture coords
    
    return vec2(oct_coord + 0.5) * (2.0f / float(side_length)) -
    vec2(1.0f, 1.0f);
}


vec3 spherical_fibonacci(float i, float n) {
    const float PHI = sqrt(5.0) * 0.5 + 0.5;
    #define madfrac(A, B) ((A) * (B)-floor((A) * (B)))
    float phi = 3.1415926 * 2 * madfrac(i, PHI - 1);
    float cos_theta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0f, 1.0f));
    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    #undef madfrac
}

// Returns +/-1
vec2  sign_not_zero(vec2 v)
{
    return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

vec3 oct_decode(vec2 oct_coord) {
    vec3 v = vec3(oct_coord.xy, 1.0 - abs(oct_coord.x) - abs(oct_coord.y));
    if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
    return normalize(v);
}

vec2 encode_to_oct(vec3 v){ 
    vec2 oct = vec2(v.xy / (abs(v.x) + abs(v.y) + abs(v.z)));
    if(v.z < 0){
        oct.xy = (1.0 - abs(oct.yx)) * sign_not_zero(oct.xy);
    }
    return oct;
}

vec2 get_probe_color_uv(ivec3 probe_coord,vec3 dir,int side_length){
    vec2 pixel_coord = vec2(get_pixel_coord_top_left(probe_coord,side_length+2));
    pixel_coord += 0.5f * vec2(side_length+2);
    pixel_coord += (encode_to_oct(dir)) * 0.5 * float(side_length);
    float texture_width = side_length == PROBE_DEPTH_SIDE ? ddgi_ubo.depth_width : ddgi_ubo.irradiance_width;
    float texture_height = side_length == PROBE_DEPTH_SIDE ? ddgi_ubo.depth_height : ddgi_ubo.irradiance_height;
    pixel_coord += vec2(0.5);
    return pixel_coord / vec2(texture_width, texture_height);
}

int get_probe_state(int probe_idx){
    //todo 
    return 1;
}


