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


struct uiData_t
{
	uint32_t indices[SHADER_MAX_INDEXES];
	vec2_t positions[SHADER_MAX_VERTEXES];
	vec2_t texCoords[SHADER_MAX_VERTEXES];
	color4ub_t colors[SHADER_MAX_VERTEXES];
	int indexCount;
	int vertexCount;
};

struct grp_t
{
	RHI::HRootSignature rootSignature;
	RHI::HPipeline pipeline;
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

struct GameplayRenderPipeline : IRenderPipeline
{
	void Init() override
	{
		{
			RHI::RootSignatureDesc desc;
			grp.rootSignature = RHI::CreateRootSignature(desc);
		}
		{
			RHI::GraphicsPipelineDesc desc;
			desc.rootSignature = grp.rootSignature;
			desc.vertexShader = RHI::CompileVertexShader(vs);
			desc.pixelShader = RHI::CompilePixelShader(ps);
			grp.pipeline = RHI::CreateGraphicsPipeline(desc);
		}
	}

	void ShutDown(qbool fullShutDown) override
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
