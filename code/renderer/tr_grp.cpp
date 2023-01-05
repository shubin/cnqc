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
// Gameplay Rendering Pipeline


#include "tr_local.h"


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

#if 0
const char* vs = R"grml(
struct VOut
{
	float4 position : SV_Position;
};

VOut main(uint id : SV_VertexID)
{
	VOut output;
	output.position.x = 1.0 * ((float)(id / 2) * 4.0 - 1.0);
	output.position.y = 1.0 * ((float)(id % 2) * 4.0 - 1.0);
	output.position.z = 0.0;
	output.position.w = 1.0;

	return output;
}
)grml";

const char* ps = R"grml(
struct VOut
{
	float4 position : SV_Position;
};

float4 main(VOut input) : SV_TARGET
{
	return float4(1, 0, 0, 1);
}
)grml";
#endif


struct ui_t
{
	typedef uint32_t index_t;
#pragma pack(push, 1)
	struct vertex_t
	{
		vec2_t position;
		vec2_t texCoords;
		uint32_t color;
	};
#pragma pack(pop)
	int maxIndexCount;
	int maxVertexCount;
	int firstIndex;
	int firstVertex;
	int indexCount;
	int vertexCount;
	RHI::HRootSignature rootSignature;
	RHI::HDescriptorTable descriptorTable;
	RHI::HPipeline pipeline;
	RHI::HBuffer indexBuffer;
	RHI::HBuffer vertexBuffer;
	index_t* indices; // @TODO: 16-bit indices
	vertex_t* vertices;
	uint32_t color;
	const shader_t* shader;
};

enum projection_t
{
	PROJECTION_NONE,
	PROJECTION_2D,
	PROJECTION_3D
};

struct grp_t
{
	ui_t ui;
	projection_t projection;
	uint32_t textureIndex;
	RHI::HSampler samplers[2];
};

static grp_t grp;

#if 0
static void DrawTriangle()
{
	RHI::CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdBindRootSignature(grp.rootSignature);
	RHI::CmdBindPipeline(grp.pipeline);
	RHI::CmdDraw(3, 0);
}
#endif

template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}

static void Begin2D()
{
	if(grp.projection == PROJECTION_2D)
	{
		return;
	}

	// @TODO: grab the right rects...
	RHI::CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdBindRootSignature(grp.ui.rootSignature);
	RHI::CmdBindPipeline(grp.ui.pipeline);
	RHI::CmdBindDescriptorTable(grp.ui.rootSignature, grp.ui.descriptorTable);
	const uint32_t stride = sizeof(ui_t::vertex_t);
	RHI::CmdBindVertexBuffers(1, &grp.ui.vertexBuffer, &stride, NULL);
	RHI::CmdBindIndexBuffer(grp.ui.indexBuffer, RHI::IndexType::UInt32, 0);
	const float scale[2] = { 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight };
	RHI::CmdSetRootConstants(grp.ui.rootSignature, RHI::ShaderType::Vertex, scale);

	grp.projection = PROJECTION_2D;
}

static void Draw2D()
{
	if(grp.ui.indexCount <= 0)
	{
		return;
	}

	const uint32_t textureIndex = grp.ui.shader->stages[0]->bundle.image[0]->textureIndex;
	const uint32_t pixelConstants[2] = { textureIndex, 0 }; // second one is the sampler index
	RHI::CmdSetRootConstants(grp.ui.rootSignature, RHI::ShaderType::Pixel, pixelConstants);
	RHI::CmdDrawIndexed(grp.ui.indexCount, grp.ui.firstIndex, 0);
	grp.ui.firstIndex += grp.ui.indexCount;
	grp.ui.firstVertex += grp.ui.vertexCount;
	grp.ui.indexCount = 0;
	grp.ui.vertexCount = 0;
}

static void Draw3D()
{
}

static void EndSurfaces()
{
	Draw2D();
	Draw3D();
}

static const void* SetColor(const void* data)
{
	const setColorCommand_t* cmd = (const setColorCommand_t*)data;

	byte* const colors = (byte*)&grp.ui.color;
	colors[0] = (byte)(cmd->color[0] * 255.0f);
	colors[1] = (byte)(cmd->color[1] * 255.0f);
	colors[2] = (byte)(cmd->color[2] * 255.0f);
	colors[3] = (byte)(cmd->color[3] * 255.0f);

	return (const void*)(cmd + 1);
}

static const void* StretchPic(const void* data)
{
	const stretchPicCommand_t* cmd = (const stretchPicCommand_t*)data;

	if(grp.ui.vertexCount + 4 > grp.ui.maxVertexCount ||
		grp.ui.indexCount + 6 > grp.ui.maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin2D();

	if(grp.ui.shader != cmd->shader)
	{
		Draw2D();
	}

	grp.ui.shader = cmd->shader;

	const int v = grp.ui.firstVertex + grp.ui.vertexCount;
	const int i = grp.ui.firstIndex + grp.ui.indexCount;
	grp.ui.vertexCount += 4;
	grp.ui.indexCount += 6;

	grp.ui.indices[i + 0] = v + 3;
	grp.ui.indices[i + 1] = v + 0;
	grp.ui.indices[i + 2] = v + 2;
	grp.ui.indices[i + 3] = v + 2;
	grp.ui.indices[i + 4] = v + 0;
	grp.ui.indices[i + 5] = v + 1;

	grp.ui.vertices[v + 0].position[0] = cmd->x;
	grp.ui.vertices[v + 0].position[1] = cmd->y;
	grp.ui.vertices[v + 0].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 0].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 0].color = grp.ui.color;

	grp.ui.vertices[v + 1].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[v + 1].position[1] = cmd->y;
	grp.ui.vertices[v + 1].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 1].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 1].color = grp.ui.color;

	grp.ui.vertices[v + 2].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[v + 2].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[v + 2].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 2].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 2].color = grp.ui.color;

	grp.ui.vertices[v + 3].position[0] = cmd->x;
	grp.ui.vertices[v + 3].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[v + 3].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 3].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 3].color = grp.ui.color;

	return (const void*)(cmd + 1);
}

static const void* Triangle(const void* data)
{
	const triangleCommand_t* cmd = (const triangleCommand_t*)data;

	if(grp.ui.vertexCount + 3 > grp.ui.maxVertexCount ||
		grp.ui.indexCount + 3 > grp.ui.maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin2D();

	if(grp.ui.shader != cmd->shader)
	{
		Draw2D();
	}

	grp.ui.shader = cmd->shader;

	const int v = grp.ui.firstVertex + grp.ui.vertexCount;
	const int i = grp.ui.firstIndex + grp.ui.indexCount;
	grp.ui.vertexCount += 3;
	grp.ui.indexCount += 3;

	grp.ui.indices[i + 0] = v + 0;
	grp.ui.indices[i + 1] = v + 1;
	grp.ui.indices[i + 2] = v + 2;

	grp.ui.vertices[v + 0].position[0] = cmd->x0;
	grp.ui.vertices[v + 0].position[1] = cmd->y0;
	grp.ui.vertices[v + 0].texCoords[0] = cmd->s0;
	grp.ui.vertices[v + 0].texCoords[1] = cmd->t0;
	grp.ui.vertices[v + 0].color = grp.ui.color;

	grp.ui.vertices[v + 1].position[0] = cmd->x1;
	grp.ui.vertices[v + 1].position[1] = cmd->y1;
	grp.ui.vertices[v + 1].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 1].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 1].color = grp.ui.color;

	grp.ui.vertices[v + 2].position[0] = cmd->x2;
	grp.ui.vertices[v + 2].position[1] = cmd->y2;
	grp.ui.vertices[v + 2].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 2].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 2].color = grp.ui.color;

	return (const void*)(cmd + 1);
}

struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
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
					data = SetColor(data);
					break;
				case RC_STRETCH_PIC:
					data = StretchPic(data);
					break;
				case RC_TRIANGLE:
					data = Triangle(data);
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
		desc.initialState = RHI::ResourceState::ShaderAccessBits | RHI::ResourceState::UnorderedAccessBit;
		desc.committedResource = true;

		image->texture = RHI::CreateTexture(desc);
		image->textureIndex = grp.textureIndex++;

		RHI::UpdateDescriptorTable(grp.ui.descriptorTable, RHI::DescriptorType::Texture, image->textureIndex, 1, &image->texture);
	}

	void UpdateTexture(image_t* image, int mipIndex, int x, int y, int width, int height, const void* data) override
	{
		RHI::TextureUploadDesc upload = { 0 };
		upload.data = data;
		upload.x = x;
		upload.y = y;
		upload.width = width;
		upload.height = height;
		RHI::UploadTextureMip0(image->texture, upload);
	}
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
