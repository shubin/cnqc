/*
===========================================================================
Copyright (C) 2022-2023 Gian 'myT' Schellenbaum

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
// Gameplay Rendering Pipeline - main interface


#include "grp_local.h"


GRP grp;


template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}


void GRP::Init()
{
	RHI::Init();

	if(firstInit)
	{
		samplers[0] = CreateSampler(SamplerDesc(TW_REPEAT, TextureFilter::Linear));
		samplers[1] = CreateSampler(SamplerDesc(TW_CLAMP_TO_EDGE, TextureFilter::Linear));

		RootSignatureDesc desc("main");
		desc.usingVertexBuffers = qtrue;
		desc.samplerCount = ARRAY_LEN(samplers);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, MAX_DRAWIMAGES * 2);
		rootSignatureDesc = desc;
		rootSignature = CreateRootSignature(desc);

		descriptorTable = CreateDescriptorTable(DescriptorTableDesc("game textures", rootSignature));

		DescriptorTableUpdate update;
		update.SetSamplers(ARRAY_LEN(samplers), samplers);
		UpdateDescriptorTable(descriptorTable, update);
	}

	textureIndex = 0;

	ui.Init();
	world.Init();
	mipMapGen.Init();
	imgui.Init();

	firstInit = false;
}

void GRP::ShutDown(bool fullShutDown)
{
	RHI::ShutDown(fullShutDown);
}

void GRP::BeginFrame()
{
	RHI::BeginFrame();
	ui.BeginFrame();
	world.BeginFrame();

	// nothing is bound to the command list yet!
	renderMode = RenderMode::None;
}

void GRP::EndFrame()
{
	ui.DrawBatch();
	imgui.Draw();
	RHI::EndFrame();
}

void GRP::AddDrawSurface(const surfaceType_t* surface, const shader_t* shader)
{
}

void GRP::ExecuteRenderCommands(const void* data)
{
	for(;;)
	{
		data = PADP(data, sizeof(void*));

		switch(*(const int*)data)
		{
			case RC_SET_COLOR:
				data = ui.SetColor(data);
				break;
			case RC_STRETCH_PIC:
				data = ui.StretchPic(data);
				break;
			case RC_TRIANGLE:
				data = ui.Triangle(data);
				break;
			case RC_DRAW_SURFS:
				world.DrawPrePass();
				data = SkipCommand<drawSurfsCommand_t>(data);
				break;
			case RC_BEGIN_FRAME:
				data = RB_BeginFrame(data);
				break;
			case RC_SWAP_BUFFERS:
				data = RB_SwapBuffers(data);
				break;
			case RC_SCREENSHOT:
				data = SkipCommand<screenshotCommand_t>(data);
				break;
			case RC_VIDEOFRAME:
				data = SkipCommand<videoFrameCommand_t>(data);
				break;
			case RC_END_OF_LIST:
				return;
			default:
				Q_assert(!"Invalid render command ID");
				return;
		}
	}
}

void GRP::CreateTexture(image_t* image, int mipCount, int width, int height)
{
	TextureDesc desc(image->name, width, height, mipCount);
	desc.shortLifeTime = true;
	if(mipCount > 1)
	{
		desc.allowedState |= ResourceStates::UnorderedAccessBit; // for mip-map generation
	}

	image->texture = ::RHI::CreateTexture(desc);
	image->textureIndex = RegisterTexture(image->texture);
}

void GRP::UpdateTexture(image_t* image, const byte* data)
{
	MappedTexture texture;
	RHI::BeginTextureUpload(texture, image->texture);
	for(uint32_t r = 0; r < texture.rowCount; ++r)
	{
		memcpy(texture.mappedData + r * texture.dstRowByteCount, data + r * texture.srcRowByteCount, texture.srcRowByteCount);
	}
	RHI::EndTextureUpload(image->texture);
		
	mipMapGen.GenerateMipMaps(image->texture);
}

void GRP::BeginTextureUpload(MappedTexture& mappedTexture, image_t* image)
{
	RHI::BeginTextureUpload(mappedTexture, image->texture);
}

void GRP::EndTextureUpload(image_t* image)
{
	RHI::EndTextureUpload(image->texture);
}

void GRP::ProcessWorld(world_t& world_)
{
	world.ProcessWorld(world_);
}

uint32_t GRP::RegisterTexture(HTexture htexture)
{
	const uint32_t index = textureIndex++;

	DescriptorTableUpdate update;
	update.SetTextures(1, &htexture, index);
	UpdateDescriptorTable(descriptorTable, update);

	return index;
}

// @TODO: move out
IRenderPipeline* renderPipeline = &grp;
