#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct StartConstants
{
	float gamma;
	float dummy0;
	float dummy1;
	float dummy2;
};

kernel void start_main(
	constant StartConstants& pc [[ buffer(0) ]],
	texture2d<uint, access::read> src [[ texture(0) ]],
	texture2d<float, access::write> dst [[ texture(1) ]],
	uint2 id [[ thread_position_in_grid ]]
)
{
	float4 v = float4(src.read(id)) / 255.0;
	dst.write(float4(pow(v.xyz, pc.gamma), v.a), id);
}

struct PassConstants
{
	float4 weights;
	int2 max_size;
	int2 scale;
	int2 offset;
	uint clamp_mode;
	uint dummy;
};

uint2 fix_coords(int2 max_size, uint clamp_mode, int2 c)
{
	if (clamp_mode > 0)
	{
		return uint2(clamp(c, int2(0, 0), max_size));
	}

	return uint2(c & max_size);
}

kernel void pass_main(
	constant PassConstants& pc [[ buffer(0) ]],
	texture2d<float, access::read> src [[ texture(0) ]],
	texture2d<float, access::write> dst [[ texture(1) ]],
	uint2 id [[ thread_position_in_grid ]]
)
{
	int2 base = int2(id) * pc.scale;
	float4 r = float4(0, 0, 0, 0);
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base - pc.offset * 3)) * pc.weights.x;
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base - pc.offset * 2)) * pc.weights.y;
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base - pc.offset * 1)) * pc.weights.z;
	r += src.read(uint2(base)) * pc.weights.w;
	r += src.read(uint2(base + pc.offset)) * pc.weights.w;
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base + pc.offset * 2)) * pc.weights.z;
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base + pc.offset * 3)) * pc.weights.y;
	r += src.read(fix_coords(pc.max_size, pc.clamp_mode, base + pc.offset * 4)) * pc.weights.x;
	dst.write(r, id);
}

struct EndConstants
{
	float4 blend_color;
	float intensity;
	float inv_gamma;
	float dummy0;
	float dummy1;
};

kernel void end_main(
	constant EndConstants& pc [[ buffer(0) ]],
	texture2d<float, access::read> src [[ texture(0) ]],
	texture2d<uint, access::write> dst [[ texture(1) ]],
	uint2 id [[ thread_position_in_grid ]]
)
{
	float4 in0 = src.read(id);
	float3 in1 = 0.5 * (in0.rgb + pc.blend_color.rgb);
	float3 in_v = mix(in0.rgb, in1.rgb, pc.blend_color.a);
	float3 out0 = pow(max(in_v, 0.0), pc.inv_gamma);
	float3 out1 = out0 * pc.intensity;
	float4 out_v = saturate(float4(out1, in0.a));
	dst.write(uint4(out_v * 255.0), id);
}
