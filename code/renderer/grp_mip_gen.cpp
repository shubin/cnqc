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


#pragma pack(push, 1)
struct RootConstants
{
	float weights[4];
	int32_t maxSize[2];
	int32_t scale[2];
	int32_t offset[2];
	uint32_t clampMode; // 0 = repeat
	uint32_t sourceMip;
};
#pragma pack(pop)

const char* cs = R"grml(
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


void mipMapGen_t::Init()
{
	{
		RootSignatureDesc desc = { 0 };
		desc.name = "mip-map gen root signature";
		desc.pipelineType = PipelineType::Compute;
		desc.constants[ShaderType::Compute].count = 12;
		desc.genericVisibility = ShaderStage::PixelBit;
		desc.AddRange(DescriptorType::RWTexture, 0, MaxTextureMips);
		rootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc = { 0 };
		desc.name = "mip-map gen descriptor table";
		desc.rootSignature = rootSignature;
		descriptorTable = CreateDescriptorTable(desc);
	}
	{
		ComputePipelineDesc desc = { 0 };
		desc.name = "mip-map gen PSO";
		desc.rootSignature = rootSignature;
		desc.shader = CompileComputeShader(cs);
		pipeline = CreateComputePipeline(desc);
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

	UpdateDescriptorTable(descriptorTable, DescriptorType::RWTexture, 0, 1, &texture);

	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, descriptorTable);

	RootConstants rc = { 0 };
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

	/*const uint32_t x = (image->width + 7) / 8;
	const uint32_t y = (image->height + 7) / 8;
	CmdDispatch(x, y, 1);*/
}
