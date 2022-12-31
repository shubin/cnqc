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


struct ui_t
{
	typedef uint32_t index_t;
#pragma pack(push, 1)
	struct vertex_t
	{
		vec2_t position;
		vec2_t texCoords;
		color4ub_t color;
	};
#pragma pack(pop)
	//uint32_t indices[SHADER_MAX_INDEXES];
	//vec2_t positions[SHADER_MAX_VERTEXES];
	//vec2_t texCoords[SHADER_MAX_VERTEXES];
	//color4ub_t colors[SHADER_MAX_VERTEXES];
	int indexCount;
	int vertexCount;
	RHI::HRootSignature rootSignature;
	RHI::HPipeline pipeline;
	RHI::HBuffer indexBuffer;
	RHI::HBuffer vertexBuffer;
	index_t* indices; // @TODO: 16-bit indices
	vertex_t* vertices;
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
	// @TODO: grab the right rects...
	RHI::CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);

	RHI::CmdBindRootSignature(grp.ui.rootSignature);
	RHI::CmdBindPipeline(grp.ui.pipeline);

	// @TODO: use vertex buffers and an index buffer
	RHI::CmdDraw(3, 0);
}

static void Draw3D()
{
}

static void EndSurfaces()
{
	Draw2D();
	Draw3D();
}

struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		{
			RHI::RootSignatureDesc desc = { 0 };
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
					data = SkipCommand<setColorCommand_t>(data);
					break;
				case RC_STRETCH_PIC:
					data = SkipCommand<stretchPicCommand_t>(data);
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
