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


// @TODO: test for OOB accesses in the shaders and return
// (also, is it needed with feature level 12.0 and HLSL 5.1/6.0 ?)


#include "grp_local.h"
namespace start
{
#include "hlsl/mip_1_cs.h"
}
namespace down
{
#include "hlsl/mip_2_cs.h"
}
namespace end
{
#include "hlsl/mip_3_cs.h"
}


#pragma pack(push, 4)

struct StartConstants
{
	float gamma;
};

struct DownConstants
{
	float weights[4];
	int32_t maxSize[2];
	int32_t scale[2];
	int32_t offset[2];
	uint32_t clampMode; // 0 = repeat
	uint32_t srcMip;
	uint32_t dstMip;
};

struct EndConstants
{
	float blendColor[4];
	float intensity;
	float invGamma; // 1.0 / gamma
	uint32_t srcMip;
	uint32_t dstMip;
};

#pragma pack(pop)


void MipMapGenerator::Init()
{
	if(!grp.firstInit)
	{
		return;
	}

	const char* stageNames[] = { "start", "down", "end" };
	uint32_t stageRCByteCount[] = { sizeof(StartConstants), sizeof(DownConstants), sizeof(EndConstants) };
	const ShaderByteCode stageShaders[] = { ShaderByteCode(start::g_cs), ShaderByteCode(down::g_cs), ShaderByteCode(end::g_cs) };

	for(int s = 0; s < 3; ++s)
	{
		Stage& stage = stages[s];
		{
			RootSignatureDesc desc(va("mip-map %s", stageNames[s]));
			desc.pipelineType = PipelineType::Compute;
			desc.constants[ShaderStage::Compute].byteCount = stageRCByteCount[s];
			desc.AddRange(DescriptorType::RWTexture, 0, MipSlice::Count + MaxTextureMips);
			stage.rootSignature = CreateRootSignature(desc);
		}
		{
			const DescriptorTableDesc desc(DescriptorTableDesc(va("mip-map %s", stageNames[s]), stage.rootSignature));
			stage.descriptorTable = CreateDescriptorTable(desc);
		}
		{
			ComputePipelineDesc desc(va("mip-map %s", stageNames[s]), stage.rootSignature);
			desc.shader = stageShaders[s];
			stage.pipeline = CreateComputePipeline(desc);
		}
	}

	for(int t = 0; t < 2; ++t)
	{
		TextureDesc desc(va("mip-map generation float16 #%d", t + 1), MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE);
		desc.format = TextureFormat::RGBA64_Float;
		desc.initialState = ResourceStates::UnorderedAccessBit;
		desc.allowedState = ResourceStates::UnorderedAccessBit | ResourceStates::ComputeShaderAccessBit;
		textures[MipSlice::Float16_0 + t] = CreateTexture(desc);
	}

	TextureDesc desc("mip-map generation unorm", MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE);
	desc.initialState = ResourceStates::UnorderedAccessBit;
	desc.allowedState = ResourceStates::UnorderedAccessBit | ResourceStates::ComputeShaderAccessBit;
	textures[MipSlice::UNorm_0] = CreateTexture(desc);
}

void MipMapGenerator::GenerateMipMaps(HTexture texture)
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
	if(image == NULL || (image->flags & IMG_NOMIPMAP) != 0)
	{
		return;
	}

	const int mipCount = R_ComputeMipCount(image->width, image->height);
	if(mipCount <= 1)
	{
		return;
	}

	HTexture textureArray[MipSlice::Count + 1];
	memcpy(textureArray, textures, sizeof(textureArray));
	textureArray[MipSlice::Count] = texture;

	for(int s = 0; s < 3; ++s)
	{
		Stage& stage = stages[s];

		DescriptorTableUpdate update;
		update.SetRWTexturesChain(ARRAY_LEN(textureArray), textureArray);
		UpdateDescriptorTable(stage.descriptorTable, update);
	}
	
	enum { GroupSize = 8, GroupMask = GroupSize - 1 };

	int w = image->width;
	int h = image->height;

	TextureBarrier barriers[MipSlice::Count];
	for(int i = 0; i < MipSlice::Count; ++i)
	{
		barriers[i] = TextureBarrier(textures[i], ResourceStates::UnorderedAccessBit);
	}

	BeginTempCommandList();
	CmdBarrier(ARRAY_LEN(barriers), barriers);

	// create a linear-space copy of mip 0 into float16 texture 0
	{
		Stage& stage = stages[Stage::Start];
		StartConstants rc = {};
		rc.gamma = r_mipGenGamma->value;

		CmdBindRootSignature(stage.rootSignature);
		CmdBindPipeline(stage.pipeline);
		CmdBindDescriptorTable(stage.rootSignature, stage.descriptorTable);
		CmdSetRootConstants(stage.rootSignature, ShaderStage::Compute, &rc);
		CmdDispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);
	}

	// overwrite mip 0 to apply r_intensity if needed
	if((image->flags & IMG_NOIMANIP) == 0 &&
		r_intensity->value != 1.0f)
	{
		Stage& stage = stages[Stage::End];
		const int destMip = 0;

		EndConstants rc = {};
		rc.intensity = r_intensity->value;
		rc.invGamma = 1.0f / r_mipGenGamma->value;
		Vector4Clear(rc.blendColor);
		rc.srcMip = MipSlice::Float16_0;
		rc.dstMip = MipSlice::Count + destMip;

		CmdBindRootSignature(stage.rootSignature);
		CmdBindPipeline(stage.pipeline);
		CmdBindDescriptorTable(stage.rootSignature, stage.descriptorTable);
		CmdSetRootConstants(stage.rootSignature, ShaderStage::Compute, &rc);
		CmdBarrier(ARRAY_LEN(barriers), barriers);
		CmdDispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);
	}

	for(int i = 1; i < mipCount; ++i)
	{
		const int w1 = w;
		const int h1 = h;
		w = max(w / 2, 1);
		h = max(h / 2, 1);

		// down-sample the image into float16 texture 1 and then 0
		{
			Stage& stage = stages[Stage::DownSample];
			DownConstants rc = {};
			rc.clampMode = image->wrapClampMode == TW_REPEAT ? 0 : 1;
			memcpy(rc.weights, tr.mipFilter, sizeof(rc.weights));

			CmdBindRootSignature(stage.rootSignature);
			CmdBindPipeline(stage.pipeline);
			CmdBindDescriptorTable(stage.rootSignature, stage.descriptorTable);

			// down-sample on the X-axis
			rc.srcMip = MipSlice::Float16_0;
			rc.dstMip = MipSlice::Float16_1;
			rc.scale[0] = w1 / w;
			rc.scale[1] = 1;
			rc.maxSize[0] = w1 - 1;
			rc.maxSize[1] = h1 - 1;
			rc.offset[0] = 1;
			rc.offset[1] = 0;
			CmdSetRootConstants(stage.rootSignature, ShaderStage::Compute, &rc);
			CmdBarrier(ARRAY_LEN(barriers), barriers);
			CmdDispatch((w + GroupMask) / GroupSize, (h1 + GroupMask) / GroupSize, 1);

			// down-sample on the Y-axis
			rc.srcMip = MipSlice::Float16_1;
			rc.dstMip = MipSlice::Float16_0;
			rc.scale[0] = 1;
			rc.scale[1] = h1 / h;
			rc.maxSize[0] = w - 1;
			rc.maxSize[1] = h1 - 1;
			rc.offset[0] = 0;
			rc.offset[1] = 1;
			CmdSetRootConstants(stage.rootSignature, ShaderStage::Compute, &rc);
			CmdBarrier(ARRAY_LEN(barriers), barriers);
			CmdDispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);
		}

		// save the results in gamma-space
		{
			Stage& stage = stages[Stage::End];
			const int destMip = i;

			EndConstants rc = {};
			rc.intensity = (image->flags & IMG_NOIMANIP) ? 1.0f : r_intensity->value;
			rc.invGamma = 1.0f / r_mipGenGamma->value;
			memcpy(rc.blendColor, r_mipBlendColors[r_colorMipLevels->integer ? destMip : 0], sizeof(rc.blendColor));
			rc.srcMip = MipSlice::Float16_0;
			rc.dstMip = MipSlice::Count + destMip;

			CmdBindRootSignature(stage.rootSignature);
			CmdBindPipeline(stage.pipeline);
			CmdBindDescriptorTable(stage.rootSignature, stage.descriptorTable);
			CmdSetRootConstants(stage.rootSignature, ShaderStage::Compute, &rc);
			CmdBarrier(ARRAY_LEN(barriers), barriers);
			CmdDispatch((w + GroupMask) / GroupSize, (h + GroupMask) / GroupSize, 1);
		}
	}

	CmdBarrier(ARRAY_LEN(barriers), barriers);
	EndTempCommandList();
}
