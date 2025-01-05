//struct VBuffer{
//    int prim_id;
//    vec3 normal;
//	float depth;
//};
//
//vec4 PackVBuffer(VBuffer v){
//    vec4 packed = vec4(0.0);
//    packed.x = float(v.prim_id);
////    packed.y = packUnorm2x16(v.normal.xy);
////    packed.z = packUnorm2x16(v.normal.z);
////    packed.w = packUnorm2x16(v.depth);
//    return packed;
//}
//
//VBuffer UnpackVBuffer(vec4 packed){
//    VBuffer v;
//    v.prim_id = int(packed.x);
////    v.normal.xy = unpackUnorm2x16(packed.y);
////    v.normal.z = unpackUnorm2x16(packed.z);
////    v.depth = unpackUnorm2x16(packed.w);
//    return v;
//}
#include "../commons.h"
struct Surface{
	int prim_id;
	vec3 position;
	vec3 normal;
	vec2 uv;
	uint material_idx;
};

layout(set = 0, binding = 7, scalar)  uniform SceneDesc_ {
	SceneDesc scene_desc;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Vertices { vec3 v[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Indices { uint i[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Normals { vec3 n[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer TexCoords { vec2 t[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Materials {
	RTMaterial m[];
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer InstanceInfo {
	RTPrimitive prim_info[];
};

Surface SurfaceFromVBuffer(vec4 vbuffer){
	int prim_id = int(vbuffer.x);
	int triangle_index = int(vbuffer.y);
	vec2 uv = vbuffer.zw;
	
//	debugPrintfEXT("prim_id:%d,triangle_index:%d,uv:%f,%f\n",prim_id,triangle_index,uv.x,uv.y);
//	Surface temp;
//	return temp;

	Materials materials = Materials(scene_desc.material_addr);
	Indices indices = Indices(scene_desc.index_addr);
	Vertices vertices = Vertices(scene_desc.vertex_addr);
	Normals normals = Normals(scene_desc.normal_addr);
	TexCoords tex_coords = TexCoords(scene_desc.uv_addr);
	InstanceInfo prim_infos = InstanceInfo(scene_desc.prim_info_addr);

	RTPrimitive pinfo = prim_infos.prim_info[prim_id];
	uint index_offset = pinfo.index_offset + 3 * triangle_index;
	uint vertex_offset = pinfo.vertex_offset;

	ivec3 ind = ivec3(indices.i[index_offset + 0], indices.i[index_offset + 1], indices.i[index_offset + 2]);
	ind += ivec3(vertex_offset);

	const vec3 v0 = vertices.v[ind.x];
	const vec3 v1 = vertices.v[ind.y];
	const vec3 v2 = vertices.v[ind.z];

	const vec3 n0 = normals.n[ind.x];
	const vec3 n1 = normals.n[ind.y];
	const vec3 n2 = normals.n[ind.z];

	const vec2 uv0 = tex_coords.t[ind.x];
	const vec2 uv1 = tex_coords.t[ind.y];
	const vec2 uv2 = tex_coords.t[ind.z];

	const vec3 barycentrics = vec3(1.0 - uv.x - uv.y, uv.x, uv.y);

	// 计算对象空间的位置和法线
	const vec3 obj_position = v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
	const vec3 obj_normal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);

	// 使用RTPrimitive中的矩阵进行变换
	const vec3 world_position = vec3(pinfo.world_matrix * vec4(obj_position, 1.0));
	const vec3 world_normal = normalize(vec3( pinfo.inverse_world_matrix) * obj_normal);  // 使用逆矩阵的转置变换法线

	const vec2 interpolated_uv = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;

	Surface surface;
	surface.position = world_position;
	surface.normal = world_normal;
	surface.uv = interpolated_uv;
	surface.material_idx = pinfo.material_index;

	return surface;
}
