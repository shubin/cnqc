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
};

static GameplayRenderPipeline pipeline; // @TODO: move out
IRenderPipeline* renderPipeline = &pipeline;
