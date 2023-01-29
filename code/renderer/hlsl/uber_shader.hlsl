/*
===========================================================================
Copyright (C) 2023 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// the uber shader is the all-encompassing world and game UI rendering shader


//
// VS macros to define:
// STAGE_COUNT 1-8
//
// PS macros to define:
// STAGE_COUNT 1-8
// STAGE#_BITS
//


#define STAGE_ATTRIBS(Index) \
	float2 texCoords##Index : TEXCOORD##Index; \
	float4 color##Index : COLOR##Index;

#if VERTEX_SHADER

struct VIn
{
	float3 position : POSITION;
	float3 normal : NORMAL;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
};

#endif

struct VOut
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
	float clipDist : SV_ClipDistance0;
};

#undef STAGE_ATTRIBS



#if VERTEX_SHADER

cbuffer RootConstants
{
	matrix modelViewMatrix;
	matrix projectionMatrix;
	float4 clipPlane;
};

#define STAGE_ATTRIBS(Index) \
	output.texCoords##Index = input.texCoords##Index; \
	output.color##Index = input.color##Index;

VOut main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.normal = input.normal;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}

#endif


#if PIXEL_SHADER

cbuffer RootConstants
{
	// @TODO: 16 bits per stage: low 12 = texture, high 4 = sampler
	//uint stageIndices[4];
	// low 16 = texture, high 16 = sampler
	uint4 stageIndices0;
	uint4 stageIndices1;
};

Texture2D textures2D[4096] : register(t0);
SamplerState samplers[12] : register(s0);

#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define		GLS_SRCBLEND_BITS					0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define		GLS_DSTBLEND_BITS					0x000000f0

#define GLS_ATEST_GT_0							0x10000000
#define GLS_ATEST_LT_80							0x20000000
#define GLS_ATEST_GE_80							0x40000000
#define		GLS_ATEST_BITS						0x70000000

float4 BlendSource(float4 src, float4 dst, uint stateBits)
{
	if(stateBits == GLS_SRCBLEND_ZERO)
		return float4(0.0, 0.0, 0.0, 0.0);
	else if(stateBits == GLS_SRCBLEND_ONE)
		return src;
	else if(stateBits == GLS_SRCBLEND_DST_COLOR)
		return src * dst;
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_DST_COLOR)
		return src * (float4(1.0, 1.0, 1.0, 1.0) - dst);
	else if(stateBits == GLS_SRCBLEND_SRC_ALPHA)
		return src * float4(src.a, src.a, src.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA)
		return src * float4(1.0 - src.a, 1.0 - src.a, 1.0 - src.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_DST_ALPHA)
		return src * float4(dst.a, dst.a, dst.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_DST_ALPHA)
		return src * float4(1.0 - dst.a, 1.0 - dst.a, 1.0 - dst.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ALPHA_SATURATE)
		return src * float4(src.a, src.a, src.a, 1.0); // ?????????
	else
		return src;
}

float4 BlendDest(float4 src, float4 dst, uint stateBits)
{
	if(stateBits == GLS_DSTBLEND_ZERO)
		return float4(0.0, 0.0, 0.0, 0.0);
	else if(stateBits == GLS_DSTBLEND_ONE)
		return dst;
	else if(stateBits == GLS_DSTBLEND_SRC_COLOR)
		return dst * src;
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR)
		return dst * float4(1.0 - src.r, 1.0 - src.g, 1.0 - src.b, 1.0 - src.a);
	else if(stateBits == GLS_DSTBLEND_SRC_ALPHA)
		return dst * float4(src.a, src.a, src.a, 1.0);
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
		return dst * float4(1.0 - src.a, 1.0 - src.a, 1.0 - src.a, 0.0);
	else if(stateBits == GLS_DSTBLEND_DST_ALPHA)
		return dst * float4(dst.a, dst.a, dst.a, 1.0);
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_DST_ALPHA)
		return dst * float4(1.0 - dst.a, 1.0 - dst.a, 1.0 - dst.a, 1.0);
	else
		return dst;
}

float4 Blend(float4 src, float4 dst, uint stateBits)
{
	float4 srcOut = BlendSource(src, dst, stateBits & GLS_SRCBLEND_BITS);
	float4 dstOut = BlendDest(src, dst, stateBits & GLS_DSTBLEND_BITS);

	return srcOut + dstOut;
}

bool FailsAlphaTest(float alpha, uint stateBits)
{
	if(stateBits == GLS_ATEST_GT_0)
		return alpha == 0.0;
	else if(stateBits == GLS_ATEST_LT_80)
		return alpha >= 0.5;
	else if(stateBits == GLS_ATEST_GE_80)
		return alpha < 0.5;
	else
		return false;
}

float4 ProcessStage(float4 color, float2 texCoords, uint textureIndex, uint samplerIndex)
{
	return color * textures2D[textureIndex].Sample(samplers[samplerIndex], texCoords);
}

void ProcessFullStage(inout float4 dst, float4 color, float2 texCoords, uint textureIndex, uint samplerIndex, uint stateBits)
{
	float4 src = ProcessStage(color, texCoords, textureIndex, samplerIndex);
	if(!FailsAlphaTest(src.a, stateBits & GLS_ATEST_BITS))
	{
		dst = Blend(src, dst, stateBits);
	}
}

// reminder: early-Z is early depth test AND early depth write
// therefore, the attribute should be gone if opaque stage #1 does alpha testing (discard)
#if (STAGE0_BITS & GLS_ATEST_BITS) == 0
[earlydepthstencil]
#endif
float4 main(VOut input) : SV_Target
{
	float4 dst = ProcessStage(input.color0, input.texCoords0, stageIndices0.x & 0xFFFF, stageIndices0.x >> 16);
	if(FailsAlphaTest(dst.a, STAGE0_BITS & GLS_ATEST_BITS))
	{
		discard;
	}
#if STAGE_COUNT >= 2
	ProcessFullStage(dst, input.color1, input.texCoords1, stageIndices0.y & 0xFFFF, stageIndices0.y >> 16, STAGE1_BITS);
#endif
#if STAGE_COUNT >= 3
	ProcessFullStage(dst, input.color2, input.texCoords2, stageIndices0.z & 0xFFFF, stageIndices0.z >> 16, STAGE2_BITS);
#endif
#if STAGE_COUNT >= 4
	ProcessFullStage(dst, input.color3, input.texCoords3, stageIndices0.w & 0xFFFF, stageIndices0.w >> 16, STAGE3_BITS);
#endif
#if STAGE_COUNT >= 5
	ProcessFullStage(dst, input.color4, input.texCoords4, stageIndices1.x & 0xFFFF, stageIndices1.x >> 16, STAGE4_BITS);
#endif
#if STAGE_COUNT >= 6
	ProcessFullStage(dst, input.color5, input.texCoords5, stageIndices1.y & 0xFFFF, stageIndices1.y >> 16, STAGE5_BITS);
#endif
#if STAGE_COUNT >= 7
	ProcessFullStage(dst, input.color6, input.texCoords6, stageIndices1.z & 0xFFFF, stageIndices1.z >> 16, STAGE6_BITS);
#endif
#if STAGE_COUNT >= 8
	ProcessFullStage(dst, input.color7, input.texCoords7, stageIndices1.w & 0xFFFF, stageIndices1.w >> 16, STAGE7_BITS);
#endif

	return dst;
}

#endif
