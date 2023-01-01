/*
===========================================================================
Copyright (C) 2022 Gian 'myT' Schellenbaum

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
struct RootConstants
{
	float2 scale;
};
ConstantBuffer<RootConstants> rc : register(b0, space0);

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

//[RootConstants(num32BitConstants=2, b0)]
VOut main(VIn input)
{
	const float2 position = input.position * rc.scale;
	VOut output;
	output.position = float4(position.x - 1.0, 1.0 - position.y, 0.0, 1.0);
	output.texCoords = input.texCoords;
	output.color = input.color;

	return output;
}
)grml";

const char* ps = R"grml(
struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
	float4 color : COLOR0;
};

float4 main(VOut input) : SV_TARGET
{
	return input.color;
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
	// limits:
	// SHADER_MAX_INDEXES
	// SHADER_MAX_VERTEXES
	int indexCount;
	int vertexCount;
	RHI::HRootSignature rootSignature;
	RHI::HPipeline pipeline;
	RHI::HBuffer indexBuffer;
	RHI::HBuffer vertexBuffer;
	index_t* indices; // @TODO: 16-bit indices
	vertex_t* vertices;
	uint32_t color;
};

struct grp_t
{
	ui_t ui;
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

static void Draw2D()
{
	if(grp.ui.indexCount <= 0)
	{
		return;
	}

	// @TODO: grab the right rects...
	RHI::CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdBindRootSignature(grp.ui.rootSignature);
	RHI::CmdBindPipeline(grp.ui.pipeline);
	const uint32_t stride = sizeof(ui_t::vertex_t);
	RHI::CmdBindVertexBuffers(1, &grp.ui.vertexBuffer, &stride, NULL);
	RHI::CmdBindIndexBuffer(grp.ui.indexBuffer, RHI::IndexType::UInt32, 0);
	const float scale[2] = { 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight };
	RHI::CmdSetRootConstants(grp.ui.rootSignature, RHI::ShaderType::Vertex, scale);

	// @TODO: use vertex buffers and an index buffer
	RHI::CmdDrawIndexed(grp.ui.indexCount, 0, 0);
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

	if(grp.ui.vertexCount + 4 > SHADER_MAX_VERTEXES ||
		grp.ui.indexCount + 6 > SHADER_MAX_INDEXES)
	{
		Draw2D();
	}
	int numVerts = grp.ui.vertexCount;
	int numIndexes = grp.ui.indexCount;
	grp.ui.vertexCount += 4;
	grp.ui.indexCount += 6;

	grp.ui.indices[numIndexes] = numVerts + 3;
	grp.ui.indices[numIndexes + 1] = numVerts + 0;
	grp.ui.indices[numIndexes + 2] = numVerts + 2;
	grp.ui.indices[numIndexes + 3] = numVerts + 2;
	grp.ui.indices[numIndexes + 4] = numVerts + 0;
	grp.ui.indices[numIndexes + 5] = numVerts + 1;

	grp.ui.vertices[numVerts].position[0] = cmd->x;
	grp.ui.vertices[numVerts].position[1] = cmd->y;
	grp.ui.vertices[numVerts].texCoords[0] = cmd->s1;
	grp.ui.vertices[numVerts].texCoords[1] = cmd->t1;
	grp.ui.vertices[numVerts].color = grp.ui.color;

	grp.ui.vertices[numVerts + 1].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[numVerts + 1].position[1] = cmd->y;
	grp.ui.vertices[numVerts + 1].texCoords[0] = cmd->s2;
	grp.ui.vertices[numVerts + 1].texCoords[1] = cmd->t1;
	grp.ui.vertices[numVerts + 1].color = grp.ui.color;

	grp.ui.vertices[numVerts + 2].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[numVerts + 2].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[numVerts + 2].texCoords[0] = cmd->s2;
	grp.ui.vertices[numVerts + 2].texCoords[1] = cmd->t2;
	grp.ui.vertices[numVerts + 2].color = grp.ui.color;

	grp.ui.vertices[numVerts + 3].position[0] = cmd->x;
	grp.ui.vertices[numVerts + 3].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[numVerts + 3].texCoords[0] = cmd->s1;
	grp.ui.vertices[numVerts + 3].texCoords[1] = cmd->t2;
	grp.ui.vertices[numVerts + 3].color = grp.ui.color;

	return (const void*)(cmd + 1);
}

struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		{
			RHI::RootSignatureDesc desc = { 0 };
			desc.name = "UI root signature";
			desc.shaderStages = RHI::ShaderStage::VertexBit;
			desc.usingVertexBuffers = true;
			desc.constants[RHI::ShaderType::Vertex].count = 2;
			grp.ui.rootSignature = RHI::CreateRootSignature(desc);
		}
		{
			RHI::GraphicsPipelineDesc desc = { 0 };
			desc.rootSignature = grp.ui.rootSignature;
			desc.vertexShader = RHI::CompileVertexShader(vs);
			desc.pixelShader = RHI::CompilePixelShader(ps);
			grp.ui.pipeline = RHI::CreateGraphicsPipeline(desc);
		}
		{
			RHI::BufferDesc desc = { 0 };
			desc.name = "UI index buffer";
			desc.byteCount = sizeof(ui_t::index_t) * SHADER_MAX_INDEXES;
			desc.memoryUsage = RHI::MemoryUsage::Upload;
			desc.initialState = RHI::ResourceState::IndexBufferBit;
			grp.ui.indexBuffer = RHI::CreateBuffer(desc);
			grp.ui.indices = (ui_t::index_t*)RHI::MapBuffer(grp.ui.indexBuffer);
		}
		{
			RHI::BufferDesc desc = { 0 };
			desc.name = "UI vertex buffer";
			desc.byteCount = sizeof(ui_t::vertex_t) * SHADER_MAX_VERTEXES;
			desc.memoryUsage = RHI::MemoryUsage::Upload;
			desc.initialState = RHI::ResourceState::VertexBufferBit;
			grp.ui.vertexBuffer = RHI::CreateBuffer(desc);
			grp.ui.vertices = (ui_t::vertex_t*)RHI::MapBuffer(grp.ui.vertexBuffer);
		}
	}

	void ShutDown(qbool fullShutDown) override
	{
	}

	void BeginFrame() override
	{
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
					data = SkipCommand<triangleCommand_t>(data);
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
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
