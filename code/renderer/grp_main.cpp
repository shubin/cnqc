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

static void EndSurfaces()
{
	grp.ui.Draw();
	grp.world.Draw();
}


struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		RHI::Init();
		grp.samplers[0] = CreateSampler(SamplerDesc(TW_REPEAT, TextureFilter::Linear));
		grp.samplers[1] = CreateSampler(SamplerDesc(TW_CLAMP_TO_EDGE, TextureFilter::Linear));
		grp.ui.Init();
		grp.world.Init();
		grp.mipMapGen.Init();
		grp.imgui.Init();
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
		EndSurfaces();
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
					EndSurfaces();
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
		if(mipCount > 1)
		{
			desc.allowedState |= ResourceStates::UnorderedAccessBit; // for mip-map generation
		}

		image->texture = ::RHI::CreateTexture(desc);
		image->textureIndex = grp.textureIndex++;

		DescriptorTableUpdate update;
		update.SetTextures(1, &image->texture, image->textureIndex);
		UpdateDescriptorTable(grp.ui.descriptorTable, update);
	}

	void UpdateTexture(image_t* image, int mipIndex, int x, int y, int width, int height, const void* data) override
	{
		Q_assert(mipIndex == 0); // @TODO: sigh...

		UploadTextureMip0(image->texture, TextureUpload(x, y, width, height, data));
	}

	void FinalizeTexture(image_t* image) override
	{
		FinishTextureUpload(image->texture);
	}

	void GenerateMipMaps(HTexture texture) override
	{
		grp.mipMapGen.GenerateMipMaps(texture);
	}

	void ProcessWorld(world_t& world) override
	{
		grp.world.ProcessWorld(world);
	}
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
