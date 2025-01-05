vec3 get_probe_offset(uint probe_idx){

    return ddgi_probe_offset_buffer.probe_offsets[probe_idx];
}

vec3 get_probe_position(ivec3 probe_coord){
    vec3 probe_grid_size = ddgi_ubo.probe_distance;
    vec3 probe_grid_origin = ddgi_ubo.probe_start_position;
    return probe_grid_origin + vec3(probe_coord) * probe_grid_size + get_probe_offset(get_probe_index_by_coord(probe_coord));
}


vec3 sample_irradiance_map(vec3 normal, vec3 position, vec3 surface_bias) {


    vec3 irradiance = vec3(0.0);
    float total_weight = 0.0;

    ivec3 base_probe_coord= get_probe_coord_by_position(position);
    vec3 base_probe_position = get_probe_position(base_probe_coord);
    vec3 biased_position = position + surface_bias;

    vec3 probe_blend_alpha = clamp((biased_position - base_probe_position) / (ddgi_ubo.probe_distance), vec3(0.0), vec3(1.0));


    for (int probe_index = 0;probe_index<8;probe_index ++){
        ivec3 adjacent_probe_offset = ivec3(probe_index, probe_index >> 1, probe_index >> 2) & ivec3(1, 1, 1);


        ivec3 adjacent_probe_coord = clamp(ivec3(base_probe_coord) + adjacent_probe_offset, ivec3(0), ivec3(ddgi_ubo.probe_counts) - ivec3(1));

        int adjacent_probe_index = get_probe_index_by_coord(adjacent_probe_coord);

        int probeState = get_probe_state(adjacent_probe_index);
        if (probeState == 0){
            continue;
        }
        vec3 adjacent_probe_position = get_probe_position(adjacent_probe_coord);
        vec3 dir_world_pos_to_adjacent_probe = normalize(adjacent_probe_position - position);
        vec3 dir_biased_pos_to_adjacent_probe = normalize(adjacent_probe_position - biased_position);


        float biased_pos_to_adjacent_probe_distance = length(adjacent_probe_position - biased_position);

        vec3 trilinear= mix(vec3(1-probe_blend_alpha), probe_blend_alpha, vec3(adjacent_probe_offset));
        trilinear = max(trilinear, vec3(0.001f));
        float trilinear_weight = trilinear.x * trilinear.y * trilinear.z;
        float weight = 1.f;


        float wrapShading = (dot(dir_world_pos_to_adjacent_probe, normal) + 1.f) * 0.5f;
        weight *= (wrapShading * wrapShading) + 0.2f;

        vec2 probe_sample_uv = get_probe_color_uv(ivec3(adjacent_probe_coord), -dir_biased_pos_to_adjacent_probe, PROBE_DEPTH_SIDE);

        vec2 filtered_distance = textureLod(dist_map, probe_sample_uv, 0).rg;
        float variance = abs(filtered_distance.x * filtered_distance.x - filtered_distance.y);

        float chebyshev_weight = 1.0f;
        if (biased_pos_to_adjacent_probe_distance > filtered_distance.x){
            float v = biased_pos_to_adjacent_probe_distance - filtered_distance.x;
            chebyshev_weight = variance / (variance + v * v);
            chebyshev_weight = max(pow(chebyshev_weight, 3), 0.f);
        }
        weight *= max(0.05f, chebyshev_weight);
        weight = max(1e-5f, weight);

        const float crush_threshold = 0.2f;
        if (weight < crush_threshold){
            weight *= (weight * weight) / (crush_threshold * crush_threshold);
        }
        

        probe_sample_uv = get_probe_color_uv(ivec3(adjacent_probe_coord), normal, PROBE_RADIANCE_SIDE);
        vec3 probeRadiance = texture(radiance_map, probe_sample_uv).rgb;
        weight *= trilinear_weight;
        irradiance += probeRadiance * weight;//* 10;
        total_weight += weight;
    }

    if (total_weight > 0.0){
        irradiance /= total_weight;
    }
    else {
        irradiance = vec3(0.0);
    }

    return irradiance * 3.1415926 * 0.85f;;
}