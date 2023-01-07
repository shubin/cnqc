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


grp_t grp;


template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}

static void EndSurfaces()
{
	grp.ui.Draw();
	// @TODO: grp.world.Draw();
}


struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		{
			SamplerDesc desc = {};
			desc.filterMode = TextureFilter::Linear;
			desc.wrapMode = TW_REPEAT;
			grp.samplers[0] = CreateSampler(desc);
			desc.wrapMode = TW_CLAMP_TO_EDGE;
			grp.samplers[1] = CreateSampler(desc);
		}
		grp.ui.Init();
		grp.mipMapGen.Init();
	}

	void ShutDown(bool fullShutDown) override
	{
	}

	void BeginFrame() override
	{
		grp.ui.BeginFrame();

		// nothing is bound to the command list yet!
		grp.projection = PROJECTION_NONE;
	}

	void EndFrame() override
	{
		EndSurfaces();
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
		TextureDesc desc = { 0 };
		desc.format = TextureFormat::RGBA32_UNorm;
		desc.width = width;
		desc.height = height;
		desc.mipCount = mipCount;
		desc.name = image->name;
		desc.sampleCount = 1;
		desc.initialState = ResourceState::PixelShaderAccessBit;
		desc.allowedState = ResourceState::PixelShaderAccessBit;
		if(mipCount > 1)
		{
			desc.allowedState |= ResourceState::UnorderedAccessBit; // for mip-map generation
		}
		desc.committedResource = true;

		image->texture = ::RHI::CreateTexture(desc);
		image->textureIndex = grp.textureIndex++;

		DescriptorTableUpdate update = { 0 };
		update.type = DescriptorType::Texture;
		update.firstIndex = image->textureIndex;
		update.resourceCount = 1;
		update.textures = &image->texture;
		UpdateDescriptorTable(grp.ui.descriptorTable, update);
	}

	void UpdateTexture(image_t* image, int mipIndex, int x, int y, int width, int height, const void* data) override
	{
		Q_assert(mipIndex == 0); // @TODO: sigh...

		TextureUpload upload = { 0 };
		upload.data = data;
		upload.x = x;
		upload.y = y;
		upload.width = width;
		upload.height = height;
		UploadTextureMip0(image->texture, upload);
	}

	void FinalizeTexture(image_t* image) override
	{
		FinishTextureUpload(image->texture);
	}

	void GenerateMipMaps(HTexture texture) override
	{
		grp.mipMapGen.GenerateMipMaps(texture);
	}
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
