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


struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		RHI::Init();

		if(grp.firstInit)
		{
			grp.samplers[0] = CreateSampler(SamplerDesc(TW_REPEAT, TextureFilter::Linear));
			grp.samplers[1] = CreateSampler(SamplerDesc(TW_CLAMP_TO_EDGE, TextureFilter::Linear));

			RootSignatureDesc desc("main");
			desc.usingVertexBuffers = qtrue;
			desc.samplerCount = ARRAY_LEN(grp.samplers);
			desc.samplerVisibility = ShaderStages::PixelBit;
			desc.genericVisibility = ShaderStages::PixelBit;
			desc.AddRange(DescriptorType::Texture, 0, MAX_DRAWIMAGES * 2);
			grp.rootSignatureDesc = desc;
			grp.rootSignature = CreateRootSignature(desc);

			grp.descriptorTable = CreateDescriptorTable(DescriptorTableDesc("game textures", grp.rootSignature));

			DescriptorTableUpdate update;
			update.SetSamplers(ARRAY_LEN(grp.samplers), grp.samplers);
			UpdateDescriptorTable(grp.descriptorTable, update);
		}

		grp.textureIndex = 0;

		grp.ui.Init();
		grp.world.Init();
		grp.mipMapGen.Init();
		grp.imgui.Init();

		grp.firstInit = false;
	}

	void ShutDown(bool fullShutDown) override
	{
		RHI::ShutDown(fullShutDown);
	}

	void BeginFrame() override
	{
		RHI::BeginFrame();
		grp.ui.BeginFrame();
		grp.world.BeginFrame();

		// nothing is bound to the command list yet!
		grp.renderMode = RenderMode::None;
	}

	void EndFrame() override
	{
		grp.ui.DrawBatch();
		grp.imgui.Draw();
		RHI::EndFrame();
	}

	void AddDrawSurface(const surfaceType_t* surface, const shader_t* shader) override
	{
	}

	void ExecuteRenderCommands(const void* data) override
	{
		for(;;)
		{
			data = PADP(data, sizeof(void*));

			switch(*(const int*)data)
			{
				case RC_SET_COLOR:
					data = grp.ui.SetColor(data);
					break;
				case RC_STRETCH_PIC:
					data = grp.ui.StretchPic(data);
					break;
				case RC_TRIANGLE:
					data = grp.ui.Triangle(data);
					break;
				case RC_DRAW_SURFS:
					grp.world.DrawPrePass();
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

	void CreateTexture(image_t* image, int mipCount, int width, int height) override
	{
		TextureDesc desc(image->name, width, height, mipCount);
		desc.shortLifeTime = true;
		if(mipCount > 1)
		{
			desc.allowedState |= ResourceStates::UnorderedAccessBit; // for mip-map generation
		}

		// @TODO: shared function for registering a new texture into the descriptor table and returning the SRV index
		image->texture = ::RHI::CreateTexture(desc);
		image->textureIndex = grp.textureIndex++;

		DescriptorTableUpdate update;
		update.SetTextures(1, &image->texture, image->textureIndex);
		UpdateDescriptorTable(grp.descriptorTable, update);
	}

	void UpdateTexture(image_t* image, const byte* data) override
	{
		MappedTexture texture;
		::BeginTextureUpload(texture, image->texture);
		for(uint32_t r = 0; r < texture.rowCount; ++r)
		{
			memcpy(texture.mappedData + r * texture.dstRowByteCount, data + r * texture.srcRowByteCount, texture.srcRowByteCount);
		}
		::EndTextureUpload(image->texture);
		
		grp.mipMapGen.GenerateMipMaps(image->texture);
	}

	void BeginTextureUpload(MappedTexture& mappedTexture, image_t* image) override
	{
		::BeginTextureUpload(mappedTexture, image->texture);
	}

	void EndTextureUpload(image_t* image) override
	{
		::EndTextureUpload(image->texture);
	}

	void ProcessWorld(world_t& world) override
	{
		grp.world.ProcessWorld(world);
	}
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
