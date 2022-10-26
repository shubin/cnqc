#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "shared.h"

#define MAX_DRAWS_PER_FRAME 400

struct DLVSData
{
	float4x4 model_view_matrix;
	float4x4 projection_matrix;
	float4   clip_plane;
	float4   os_light_pos;
	float4   os_eye_pos;
};

struct PushConstant
{
	float  light_radius;
	float  opaque;
	float  intensity;
	float  greyscale;
	float4 light_color;
};

struct VertexIn
{
	float4 position [[attribute(0)]];
	float4 color [[attribute(1)]];
	float2 texcoord0 [[attribute(2)]];
	float2 texcoord1 [[attribute(3)]];
	float4 normal[[attribute(4)]];
};

struct VertexOut
{
	float4 position [[position]];
	float3 normal[[user(locn0)]];
	float2 texcoord0[[user(locn1)]];
	float3 os_light_vec[[user(locn2)]];
	float3 os_eye_vec[[user(locn3)]];
};

vertex VertexOut v_main(
	VertexIn in [[stage_in]],
	constant PushConstant& pc[[buffer(5)]],
	constant DLVSData ubo[[buffer(7)]][MAX_DRAWS_PER_FRAME]
)
{
	VertexOut out;
	uint ubo_index   = uint(pc.light_color.a);
	float4 position  = ubo[ubo_index].model_view_matrix * float4(in.position.xyz, 1);
 	out.position     = ubo[ubo_index].projection_matrix * position;
	out.normal       = in.normal.xyz;
	out.texcoord0    = in.texcoord0;
	out.os_light_vec = ubo[ubo_index].os_light_pos.xyz - in.position.xyz;
	out.os_eye_vec   = ubo[ubo_index].os_eye_pos.xyz - in.position.xyz;
	return out;
}

float bezier_ease(float t)
{
	return t * t * (3.0 - 2.0 * t);
}

fragment float4 f_main(
	VertexOut in [[stage_in]],
	constant PushConstant& pc[[buffer(5)]],
	sampler s0 [[sampler(0)]],
	texture2d<float> texture0 [[texture(0)]]
)
{
	float4 base = make_greyscale(texture0.sample(s0, in.texcoord0), pc.greyscale);
	float3 nL = normalize(in.os_light_vec); // normalized object-space light vector
	float3 nV = normalize(in.os_eye_vec); // normalized object-space view vector
	float3 nN = in.normal; // normalized object-space normal vector

	// light intensity
	float intens_factor = min(dot(in.os_light_vec, in.os_light_vec) * pc.light_radius, 1.0);
	float3 intens = pc.light_color.rgb * bezier_ease(1.0 - sqrt(intens_factor));

	// specular reflection term (N.H)
	float spec_factor = min(abs(dot(nN, normalize(nL + nV))), 1.0);
	float spec = pow(spec_factor, 16.0) * 0.25;

	// Lambertian diffuse reflection term (N.L)
	float diffuse = min(abs(dot(nN, nL)), 1.0);
	float3 color = (base.rgb * float3(diffuse) + float3(spec)) * intens * pc.intensity;
	float alpha = mix(pc.opaque, 1.0, base.a);
	return float4(color.rgb * alpha, alpha);
}
