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


const char* cs = R"grml(
// @TODO:
)grml";


void mipMapGen_t::Init()
{
	{
		RootSignatureDesc desc = { 0 };
		desc.name = "mip-map gen root signature";
		desc.constants[ShaderType::Compute].count = 0;
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

	// @TODO:
	//UpdateDescriptorTable();

	//const int mipCount = R_ComputeMipCount(image->width, image->height);
}
