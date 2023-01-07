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
// Gameplay Rendering Pipeline - texture mip-map generation


#include "grp_local.h"


#pragma pack(push, 4)
struct StartConstants
{
	float gamma;
};
#pragma pack(pop)

const char* start_cs = R"grml(
// gamma-space to linear-space compute shader

RWTexture2D<float4> src : register(u0);
RWTexture2D<float4> dst : register(u16);

cbuffer RootConstants
{
	float gamma;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	float4 v = src[id.xy];
	dst[id.xy] = float4(pow(v.xyz, gamma), v.a);
}
)grml";

#pragma pack(push, 4)
struct DownConstants
{
	float weights[4];
	int32_t maxSize[2];
	int32_t scale[2];
	int32_t offset[2];
	uint32_t clampMode; // 0 = repeat
	uint32_t sourceMip;
};
#pragma pack(pop)

const char* down_cs = R"grml(
// 8-tap 1D filter compute shader

RWTexture2D<float4> mips[16] : register(u0);

cbuffer RootConstants
{
	float4 weights;
	int2 maxSize;
	int2 scale;
	int2 offset;
	uint clampMode; // 0 = repeat
	uint sourceMip;
}

uint2 FixCoords(int2 c)
{
	if(clampMode > 0)
	{
		// clamp
		return uint2(clamp(c, int2(0, 0), maxSize));
	}

	// repeat
	return uint2(c & maxSize);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	RWTexture2D<float4> src = mips[sourceMip + 0];
	RWTexture2D<float4> dst = mips[sourceMip + 1];

	int2 base = int2(id.xy) * scale;
	float4 r = float4(0, 0, 0, 0);
	r += src[FixCoords(base - offset * 3)] * weights.x;
	r += src[FixCoords(base - offset * 2)] * weights.y;
	r += src[FixCoords(base - offset * 1)] * weights.z;
	r += src[          base              ] * weights.w;
	r += src[          base + offset     ] * weights.w;
	r += src[FixCoords(base + offset * 2)] * weights.z;
	r += src[FixCoords(base + offset * 3)] * weights.y;
	r += src[FixCoords(base + offset * 4)] * weights.x;
	dst[id.xy] = r;
}
)grml";

#pragma pack(push, 4)
struct EndConstants
{
	float blendColor[4];
	float intensity;
	float invGamma; // 1.0 / gamma
	int sourceIndex; // 16 or 32
};
#pragma pack(pop)

const char* end_cs = R"grml(
// linear-space to gamma-space compute shader

RWTexture2D<float4> source[32] : register(u16);
RWTexture2D<float4> dst        : register(u0);

cbuffer RootConstants
{
	float4 blendColor;
	float intensity;
	float invGamma; // 1.0 / gamma
	int sourceIndex; // 16 or 32
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	RWTexture2D<float4> src = source[sourceIndex];

	// yes, intensity *should* be done in light-linear space
	// but we keep the old behavior for consistency...
	float4 in0 = src[id.xy];
	float3 in1 = 0.5 * (in0.rgb + blendColor.rgb);
	float3 inV = lerp(in0.rgb, in1.rgb, blendColor.a);
	float3 out0 = pow(max(inV, 0.0), invGamma);
	float3 out1 = out0 * intensity;
	float4 outV = saturate(float4(out1, in0.a));
	dst[id.xy] = outV;
}
)grml";


void mipMapGen_t::Init()
{
	const char* stageNames[] = { "start", "down", "end" };
	uint32_t stageRCByteCount[] = { sizeof(StartConstants), sizeof(DownConstants), sizeof(EndConstants) };
	const char* stageShaders[] = { start_cs, down_cs, end_cs };

	for(int s = 0; s < 3; ++s)
	{
		Stage& stage = stages[s];
		{
			RootSignatureDesc desc = { 0 };
			desc.name = va("mip-map %s root signature", stageNames[s]);
			desc.pipelineType = PipelineType::Compute;
			desc.constants[ShaderType::Compute].count = stageRCByteCount[s] / 4;
			desc.genericVisibility = ShaderStage::ComputeBit;
			desc.AddRange(DescriptorType::RWTexture, 0, MaxTextureMips * 3);
			stage.rootSignature = CreateRootSignature(desc);
		}
		{
			DescriptorTableDesc desc = { 0 };
			desc.name = va("mip-map %s descriptor table", stageNames[s]);
			desc.rootSignature = stage.rootSignature;
			stage.descriptorTable = CreateDescriptorTable(desc);
		}
		{
			ComputePipelineDesc desc = { 0 };
			desc.name = va("mip-map %s PSO", stageNames[s]);
			desc.rootSignature = stage.rootSignature;
			desc.shader = CompileComputeShader(stageShaders[s]);
			stage.pipeline = CreateComputePipeline(desc);
		}
	}

	for(int t = 0; t < 2; ++t)
	{
		TextureDesc desc = { 0 };
		desc.name = va("mip-map generation texture #%d", t + 1);
		desc.format = TextureFormat::RGBA64_Float;
		desc.width = MAX_TEXTURE_SIZE;
		desc.height = MAX_TEXTURE_SIZE;
		desc.mipCount = 1;
		desc.sampleCount = 1;
		desc.initialState = ResourceState::UnorderedAccessBit;
		desc.allowedState = ResourceState::UnorderedAccessBit | ResourceState::ComputeShaderAccessBit;
		desc.committedResource = true;
		textures[t] = CreateTexture(desc);
	}
}

void mipMapGen_t::GenerateMipMaps(HTexture texture)
{
	// @FIXME:
	image_t* image = NULL;
	for(int i = 0; i < tr.numImages; ++i)
	{
		if(tr.images[i]->texture == texture)
		{
			image = tr.images[i];
			break;
		}
	}
	Q_assert(image);
	if(image == NULL)
	{
		return;
	}

	/*if(Q_stricmp(image->name, "icons/envirosuit.tga"))
	{
		return;
	}*/

	for(int s = 0; s < 3; ++s)
	{
		Stage& stage = stages[s];
		uint32_t index = 0;

		DescriptorTableUpdate update = { 0 };
		update.type = DescriptorType::RWTexture;
		update.resourceCount = 1;

		update.firstIndex = index++;
		update.textures = &textures[0];
		UpdateDescriptorTable(stage.descriptorTable, update);

		update.firstIndex = index++;
		update.textures = &textures[0];
		UpdateDescriptorTable(stage.descriptorTable, update);

		update.firstIndex = index++;
		update.textures = &textures[0]; // @TODO:
		UpdateDescriptorTable(stage.descriptorTable, update);

		update.firstIndex = index++;
		update.textures = &texture;
		UpdateDescriptorTable(stage.descriptorTable, update);
	}
	
	enum { GroupSize = 8, GroupMask = GroupSize - 1 };

	int w = image->width;
	int h = image->height;

	// create a linear color space copy of mip 0 into temp texture 0
	{
		Stage& stage = stages[0];
		StartConstants rc = {};
		rc.gamma = r_mipGenGamma->value;

		CmdBindRootSignature(stage.rootSignature);
		CmdBindPipeline(stage.pipeline);
		CmdBindDescriptorTable(stage.rootSignature, stage.descriptorTable);
		CmdSetRootConstants(stage.rootSignature, ShaderType::Compute, &rc);
		CmdDispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);
	}

#if 0
	DownConstants rc = { 0 };
	rc.clampMode = image->wrapClampMode == TW_REPEAT ? 0 : 1;
	memcpy(rc.weights, tr.mipFilter, sizeof(rc.weights));

	//const int mipCount = R_ComputeMipCount(image->width, image->height);

	enum { GroupSize = 8, GroupMask = GroupSize - 1 };

	int w = image->width;
	int h = image->height;
	//for(int i = 1; i < mipCount; ++i)
	{
		const int w1 = w;
		const int h1 = h;
		w = max(w / 2, 1);
		h = max(h / 2, 1);

		// down-sample on the X-axis
		rc.scale[0] = w1 / w;
		rc.scale[1] = 1;
		rc.maxSize[0] = w1 - 1;
		rc.maxSize[1] = h1 - 1;
		rc.offset[0] = 1;
		rc.offset[1] = 0;
		rc.sourceMip = 0;
		CmdSetRootConstants(rootSignature, ShaderType::Compute, &rc);
		CmdDispatch((w + GroupMask) / GroupSize, (h1 + GroupMask) / GroupSize, 1);
	}
#endif

	/*const uint32_t x = (image->width + 7) / 8;
	const uint32_t y = (image->height + 7) / 8;
	CmdDispatch(x, y, 1);*/
}
