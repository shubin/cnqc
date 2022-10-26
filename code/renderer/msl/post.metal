#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "shared.h"

struct PushConstant
{
	float scale_x;
	float scale_y;
	float gamma;
	float brightness;
	float greyscale;
	float pad;
};

struct VertexOut
{
    float4 position [[position]];
    float2 texcoord [[user(locn0)]];
};

vertex VertexOut v_main(
	constant PushConstant& pc [[buffer(0)]],
	uint v_id [[vertex_id]]
)
{
	VertexOut output;
	output.position.x = pc.scale_x * ( float( v_id / 2 ) * 4.0 - 1.0 );
	output.position.y = pc.scale_y * ( float( v_id % 2 ) * 4.0 - 1.0 );
	output.position.z = 0.0;
	output.position.w = 1.0;
	output.texcoord.x = float( v_id / 2 ) * 2.0;
	output.texcoord.y = 1.0f - float( v_id % 2 ) * 2.0;
	return output;
}

fragment float4 f_main(
	VertexOut input [[stage_in]],
	constant PushConstant& pc [[buffer(0)]],
	sampler s [[sampler(0)]],
	texture2d<float> texture0 [[texture(0)]]
)
{
	float3 base = texture0.sample(s, input.texcoord).rgb;
	float3 gc = pow( base, float3( pc.gamma ) ) * pc.brightness;
	return make_greyscale( float4( gc, 1.0f ), pc.greyscale );
}
