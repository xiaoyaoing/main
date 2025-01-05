uint surfel_cellindex(ivec3 cell){
	const uint p1 = 73856093;   // some large primes
	const uint p2 = 19349663;
	const uint p3 = 83492791;
	uint n = p1 * cell.x ^ p2 * cell.y ^ p3 * cell.z;
	n %= SURFEL_TABLE_SIZE;
	return n;
}

//ivec3 floor(vec3 v){
//	return ivec3(int(v.x), int(v.y), int(v.z));
//}

ivec3 surfel_cell(vec3 position){
	return ivec3((position - per_frame.camera_pos) / SURFEL_MAX_RADIUS) + SURFEL_GRID_DIMENSIONS / 2;
}

vec2 get_surrfel_uv(){
	return vec2(0,0);
}

bool surfel_cellintersects(Surfel surfel, ivec3 cell) {
	vec3 cell_min = vec3(cell) * SURFEL_CELL_SIZE;
	vec3 cell_max = cell_min + vec3(SURFEL_CELL_SIZE);

	// 球体-AABB相交测试
	vec3 closest = max(cell_min, min(surfel.position, cell_max));
	float dist2 = dot(closest - surfel.position,
	closest - surfel.position);
	return dist2 <= (surfel.radius * surfel.radius);
}
