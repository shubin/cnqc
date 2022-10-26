#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

float4 make_greyscale(float4 color, float amount)
{
	float grey = dot(color.rgb, float3(0.299, 0.587, 0.114));
	float4 result = mix(color, float4(grey, grey, grey, color.a), amount);

	return result;
}
