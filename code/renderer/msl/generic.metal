#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

#include "shared.h"

#define MAX_DRAWS_PER_FRAME 400

struct GenericVSData
{
	float4x4 model_view_matrix;
	float4x4 projection_matrix;
	float4 clip_plane;
};

struct PushConstant
{
	uint instance_id;
  uint alpha_test;
	uint tex_env;
	float seed;
	float greyscale;
	float inv_gamma;
	float inv_brightness;
	float noise_scale;
	float alpha_boost;
};

struct VertexIn
{
	float4 position [[attribute(0)]];
	float4 color [[attribute(1)]];
	float2 texcoord0 [[attribute(2)]];
	float2 texcoord1 [[attribute(3)]];
};

struct VertexOut
{
	float4 position [[position]];
	float4 color [[user(locn0)]];
	float2 texcoord0 [[user(locn1)]];
	float2 texcoord1 [[user(locn2)]];
};

vertex VertexOut v_main(
	VertexIn in [[stage_in]],
	constant PushConstant& pc[[buffer(5)]],
	constant GenericVSData transforms[[buffer(6)]][MAX_DRAWS_PER_FRAME]
)
{
	VertexOut out;
	float4 position = transforms[pc.instance_id].model_view_matrix * float4(in.position.xyz, 1);
 	out.position = transforms[pc.instance_id].projection_matrix * position;
	out.color       = in.color;
	out.texcoord0   = in.texcoord0;
	out.texcoord1   = in.texcoord1;
  // gl_ClipDistance[0] = dot(position, clip_plane);
	return out;
}

#ifdef CNQ3_DITHER
float hash(float2 input)
{
	// this is from Morgan McGuire's "Hashed Alpha Testing" paper
	return frac(1.0e4 * sin(17.0 * input.x + 0.1 * input.y) + (0.1 + abs(sin(13.0 * input.y + input.x))));
}

float linearize(float color)
{
	return pow(abs(color * pc.inv_brightness), pc.inv_gamma) * sign(color);
}

float4 dither(float4 color, float3 position)
{
	float2 new_seed = position.xy + float2(0.6849, 0.6849) * seed + float2(position.z, position.z);
	float noise = (pc.noise_scale / 255.0) * linearize(hash(new_seed) - 0.5);

	return color + float4(noise, noise, noise, 0.0);
}
#endif

#ifdef CNQ3_A2C
float correct_alpha(float threshold, float alpha, float2 tc)
{
  float2 size = float2(textureSize(sampler2D(u_texture1, u_sampler1), 0));
	if(min(size.x, size.y) <= 8.0)
		return alpha >= threshold ? 1.0 : 0.0;
	alpha *= 1.0 + pc.alpha_boost * textureQueryLod(sampler2D(u_texture0, u_sampler0), tc).x;
	float2 dtc = fwidth(tc * float2(size.x, size.y));
	float rec_scale = max(0.25 * (dtc.x + dtc.y), 1.0 / 16384.0);
	float scale = max(1.0 / rec_scale, 1.0);
	float ac = threshold + (alpha - threshold) * scale;

	return ac;
}
#endif

fragment float4 f_main(
	VertexOut in [[stage_in]],
	constant PushConstant& pc [[buffer(5)]],
	sampler s0 [[sampler(0)]],
	sampler s1 [[sampler(1)]],
	texture2d<float> texture0 [[texture(0)]],
	texture2d<float> texture1 [[texture(1)]]
)
{
	float4 s = texture1.sample(s1, in.texcoord1);
	float4 p = texture0.sample(s0, in.texcoord0);
	float4 r;

	switch(pc.tex_env)
	{
		case 1:
			r = in.color * s * p;
			break;
		case 2:
			r = s; // use input.color or not?
			break;
		case 3:
			r = in.color * float4(p.rgb * (1 - s.a) + s.rgb * s.a, p.a);
			break;
		case 4:
			r = in.color * float4(p.rgb + s.rgb, p.a * s.a);
			break;
		default:
			r = in.color * p;
			break;
	}

#ifdef CNQ3_DITHER
	r = dither(r, in.position.xyz);
#endif

	if((pc.alpha_test == uint(1) && r.a == 0.0) ||
	   (pc.alpha_test == uint(2) && r.a >= 0.5) ||
	   (pc.alpha_test == uint(3) && r.a <  0.5))
	{
	  discard_fragment();
	}

	r = make_greyscale(r, pc.greyscale);
	return r;
}

