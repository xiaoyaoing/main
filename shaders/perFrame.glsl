layout(set = 0, binding = 0) uniform _GlobalFrameUniform {
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 proj;
    mat4 view;
    mat4 inv_proj;
    mat4 inv_view;

	float z_far;
	float z_near;
	float padding1;
	float padding2;


    vec3 camera_pos;

    uint light_count;

    ivec2 resolution;
    ivec2 inv_resolution;

    float roughness_scale;
    float normal_scale;
    float roughness_override;
    int  use_roughness_override;

} per_frame;


//layout(set = 0, binding = 1) uniform _GlobalPrimitiveUniform {
//    mat4 model;
//} per_primitive;


struct PerPrimitive {
    mat4 model;
    mat4 modelIT;
    uint material_index;
    uint padding1;
    uint padding2;
    uint padding3;
};





vec3  worldPosFromDepth    (vec2 uv, float depth){
    vec4  clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world_w = per_frame.inv_view_proj * clip;
    vec3 pos     = world_w.xyz / world_w.w;
    return pos;
}




