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


const char* vs = R"grml(
cbuffer RootConstants
{
	float2 scale;
};

struct VIn
{
	float2 position : POSITION;
	float2 texCoords : TEXCOORD0;
	float4 color : COLOR0;
};

struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
	float4 color : COLOR0;
};

VOut main(VIn input)
{
	const float2 position = input.position * scale;
	VOut output;
	output.position = float4(position.x - 1.0, 1.0 - position.y, 0.0, 1.0);
	output.texCoords = input.texCoords;
	output.color = input.color;

	return output;
}
)grml";

const char* ps = R"grml(
cbuffer RootConstants
{
	uint textureIndex;
	uint samplerIndex;
};

Texture2D textures2D[2048] : register(t0);
SamplerState samplers[2] : register(s0);

struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
	float4 color : COLOR0;
};

float4 main(VOut input) : SV_TARGET
{
	return textures2D[textureIndex].Sample(samplers[samplerIndex], input.texCoords) * input.color;
}
)grml";


template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}

static void EndSurfaces()
{
	grp.ui.Draw();
	//grp.world.Draw();
}


struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		RHI::HTexture nullTexture;
		{
			RHI::TextureDesc desc = { 0 };
			desc.name = "null texture";
			desc.format = RHI::TextureFormat::RGBA32_UNorm;
			desc.initialState = RHI::ResourceState::PixelShaderAccessBit;
			desc.allowedState = RHI::ResourceState::PixelShaderAccessBit;
			desc.mipCount = 1;
			desc.sampleCount = 1;
			desc.width = 1;
			desc.height = 1;
			desc.committedResource = true;
			nullTexture = RHI::CreateTexture(desc);
		}
		{
			const uint8_t color[4] = { 0, 0, 0, 255 };
			RHI::TextureUpload desc = { 0 };
			desc.x = 0;
			desc.y = 0;
			desc.width = 1;
			desc.height = 1;
			desc.data = color;
			RHI::UploadTextureMip0(nullTexture, desc);
		}
		{
			RHI::SamplerDesc desc = {};
			desc.filterMode = RHI::TextureFilter::Linear;
			desc.wrapMode = TW_REPEAT;
			grp.samplers[0] = RHI::CreateSampler(desc);
			desc.wrapMode = TW_CLAMP_TO_EDGE;
			grp.samplers[1] = RHI::CreateSampler(desc);
		}
		{
			RHI::RootSignatureDesc desc = { 0 };
			desc.name = "UI root signature";
			desc.usingVertexBuffers = qtrue;
			desc.constants[RHI::ShaderType::Vertex].count = 2;
			desc.constants[RHI::ShaderType::Pixel].count = 2;
			desc.samplerCount = ARRAY_LEN(grp.samplers);
			desc.samplerVisibility = RHI::ShaderStage::PixelBit;
			desc.genericVisibility = RHI::ShaderStage::PixelBit;
			desc.AddRange(RHI::DescriptorType::Texture, 0, MAX_DRAWIMAGES);
			grp.ui.rootSignature = RHI::CreateRootSignature(desc);
		}
		{
			RHI::DescriptorTableDesc desc = { 0 };
			desc.name = "UI descriptor table";
			desc.rootSignature = grp.ui.rootSignature;
			grp.ui.descriptorTable = RHI::CreateDescriptorTable(desc);
			RHI::InitDescriptorTable(grp.ui.descriptorTable, RHI::DescriptorType::Texture, 0, MAX_DRAWIMAGES, &nullTexture);
			RHI::UpdateDescriptorTable(grp.ui.descriptorTable, RHI::DescriptorType::Sampler, 0, ARRAY_LEN(grp.samplers), grp.samplers);
		}
		{
			RHI::GraphicsPipelineDesc desc = { 0 };
			desc.name = "UI PSO";
			desc.rootSignature = grp.ui.rootSignature;
			desc.vertexShader = RHI::CompileVertexShader(vs);
			desc.pixelShader = RHI::CompilePixelShader(ps);
			desc.vertexLayout.bindingStrides[0] = sizeof(ui_t::vertex_t);
			desc.vertexLayout.AddAttribute(0, RHI::ShaderSemantic::Position,
				RHI::DataType::Float32, 2, offsetof(ui_t::vertex_t, position));
			desc.vertexLayout.AddAttribute(0, RHI::ShaderSemantic::TexCoord,
				RHI::DataType::Float32, 2, offsetof(ui_t::vertex_t, texCoords));
			desc.vertexLayout.AddAttribute(0, RHI::ShaderSemantic::Color,
				RHI::DataType::UNorm8, 4, offsetof(ui_t::vertex_t, color));
			desc.depthStencil.depthComparison = RHI::ComparisonFunction::Always;
			desc.depthStencil.depthStencilFormat = RHI::TextureFormat::DepthStencil32_UNorm24_UInt8;
			desc.depthStencil.enableDepthTest = false;
			desc.depthStencil.enableDepthWrites = false;
			desc.rasterizer.cullMode = RHI::CullMode::None;
			desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, RHI::TextureFormat::RGBA32_UNorm);
			grp.ui.pipeline = RHI::CreateGraphicsPipeline(desc);
		}
		grp.ui.maxVertexCount = 64 << 10;
		grp.ui.maxIndexCount = 8 * grp.ui.maxVertexCount;
		{
			RHI::BufferDesc desc = { 0 };
			desc.name = "UI index buffer";
			desc.byteCount = sizeof(ui_t::index_t) * grp.ui.maxIndexCount * RHI::FrameCount;
			desc.memoryUsage = RHI::MemoryUsage::Upload;
			desc.initialState = RHI::ResourceState::IndexBufferBit;
			grp.ui.indexBuffer = RHI::CreateBuffer(desc);
			grp.ui.indices = (ui_t::index_t*)RHI::MapBuffer(grp.ui.indexBuffer);
			
		}
		{
			RHI::BufferDesc desc = { 0 };
			desc.name = "UI vertex buffer";
			desc.byteCount = sizeof(ui_t::vertex_t) * grp.ui.maxVertexCount * RHI::FrameCount;
			desc.memoryUsage = RHI::MemoryUsage::Upload;
			desc.initialState = RHI::ResourceState::VertexBufferBit;
			grp.ui.vertexBuffer = RHI::CreateBuffer(desc);
			grp.ui.vertices = (ui_t::vertex_t*)RHI::MapBuffer(grp.ui.vertexBuffer);
		}
	}

	void ShutDown(bool fullShutDown) override
	{
	}

	void BeginFrame() override
	{
		// move to this frame's dedicated buffer section
		const uint32_t frameIndex = RHI::GetFrameIndex();
		grp.ui.firstIndex = frameIndex * grp.ui.maxIndexCount;
		grp.ui.firstVertex = frameIndex * grp.ui.maxVertexCount;

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
		RHI::TextureDesc desc = { 0 };
		desc.format = RHI::TextureFormat::RGBA32_UNorm;
		desc.width = width;
		desc.height = height;
		desc.mipCount = mipCount;
		desc.name = image->name;
		desc.sampleCount = 1;
		desc.initialState = RHI::ResourceState::PixelShaderAccessBit;
		desc.allowedState = RHI::ResourceState::PixelShaderAccessBit;
		if(mipCount > 1)
		{
			desc.allowedState |= RHI::ResourceState::UnorderedAccessBit; // for mip-map generation
		}
		desc.committedResource = true;

		image->texture = RHI::CreateTexture(desc);
		image->textureIndex = grp.textureIndex++;

		RHI::UpdateDescriptorTable(grp.ui.descriptorTable, RHI::DescriptorType::Texture, image->textureIndex, 1, &image->texture);
	}

	void UpdateTexture(image_t* image, int mipIndex, int x, int y, int width, int height, const void* data) override
	{
		Q_assert(mipIndex == 0); // @TODO: sigh...

		RHI::TextureUpload upload = { 0 };
		upload.data = data;
		upload.x = x;
		upload.y = y;
		upload.width = width;
		upload.height = height;
		RHI::UploadTextureMip0(image->texture, upload);
	}

	void FinalizeTexture(image_t* image) override
	{
		RHI::FinishTextureUpload(image->texture);
	}

	void GenerateMipMaps(RHI::HTexture texture) override
	{
		//RHI::UpdateDescriptorTable();
	}
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
