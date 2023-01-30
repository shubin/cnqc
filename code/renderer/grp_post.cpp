/*
===========================================================================
Copyright (C) 2023 Gian 'myT' Schellenbaum

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
// Gameplay Rendering Pipeline - post-process pass


#include "grp_local.h"
#include "hlsl/post_gamma_vs.h"
#include "hlsl/post_gamma_ps.h"


#pragma pack(push, 4)

struct PostVertexRC
{
	float scaleX;
	float scaleY;
};

struct PostPixelRC
{
	float invGamma;
	float brightness;
	float greyscale;
};

#pragma pack(pop)


void PostProcess::Init()
{
	if(!grp.firstInit)
	{
		return;
	}

	{
		RootSignatureDesc desc("Post Process");
		desc.usingVertexBuffers = false;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(PostVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(PostPixelRC);
		desc.samplerCount = 1;
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, 1);
		desc.genericVisibility = ShaderStages::PixelBit;
		rootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc("Post Process", rootSignature);
		descriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update;
		update.SetSamplers(1, &grp.samplers[GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear)]);
		UpdateDescriptorTable(descriptorTable, update);
	}
	{
		GraphicsPipelineDesc desc("Post Process", rootSignature);
		desc.vertexShader = ShaderByteCode(g_vs);
		desc.pixelShader = ShaderByteCode(g_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_TWO_SIDED;
		desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}
}

void PostProcess::Draw()
{
	SCOPED_RENDER_PASS("Post-process", 0.125f, 0.125f, 0.5f);

	const HTexture swapChain = GetSwapChainTexture();
	const TextureBarrier barriers[2] =
	{
		TextureBarrier(grp.renderTarget, ResourceStates::PixelShaderAccessBit),
		TextureBarrier(swapChain, ResourceStates::RenderTargetBit)
	};
	CmdBarrier(ARRAY_LEN(barriers), barriers);

	DescriptorTableUpdate update;
	update.SetTextures(1, &grp.renderTarget);
	UpdateDescriptorTable(descriptorTable, update);

	// @TODO: r_blitMode support
	PostVertexRC vertexRC = {};
	vertexRC.scaleX = 1.0f;
	vertexRC.scaleY = 1.0f;

	PostPixelRC pixelRC = {};
	pixelRC.invGamma = 1.0f / r_gamma->value;
	pixelRC.brightness = r_brightness->value;
	pixelRC.greyscale = r_greyscale->value;

	CmdSetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdBindRenderTargets(1, &swapChain, NULL);
	CmdBindPipeline(pipeline);
	CmdBindRootSignature(rootSignature);
	CmdBindDescriptorTable(rootSignature, descriptorTable);
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, &vertexRC);
	CmdSetRootConstants(rootSignature, ShaderStage::Pixel, &pixelRC);
	CmdDraw(3, 0);
}
