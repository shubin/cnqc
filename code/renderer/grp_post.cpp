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
namespace tone_map
{
#include "hlsl/post_gamma_vs.h"
#include "hlsl/post_gamma_ps.h"
}
namespace inverse_tone_map
{
#include "hlsl/post_inverse_gamma_vs.h"
#include "hlsl/post_inverse_gamma_ps.h"
}


#pragma pack(push, 4)

struct GammaVertexRC
{
	float scaleX;
	float scaleY;
};

struct GammaPixelRC
{
	float invGamma;
	float brightness;
	float greyscale;
};

struct InverseGammaPixelRC
{
	float gamma;
	float invBrightness;
};

#pragma pack(pop)


void PostProcess::Init()
{
	if(!grp.firstInit)
	{
		return;
	}

	{
		RootSignatureDesc desc("tone map");
		desc.usingVertexBuffers = false;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(GammaVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(GammaPixelRC);
		desc.samplerCount = 1;
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, 1);
		desc.genericVisibility = ShaderStages::PixelBit;
		toneMapRootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc("tone map", toneMapRootSignature);
		toneMapDescriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update;
		update.SetSamplers(1, &grp.samplers[GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear)]);
		UpdateDescriptorTable(toneMapDescriptorTable, update);
	}
	{
		GraphicsPipelineDesc desc("tone map", toneMapRootSignature);
		desc.vertexShader = ShaderByteCode(tone_map::g_vs);
		desc.pixelShader = ShaderByteCode(tone_map::g_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_TWO_SIDED;
		desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
		toneMapPipeline = CreateGraphicsPipeline(desc);
	}

	{
		RootSignatureDesc desc("inverse tone map");
		desc.usingVertexBuffers = false;
		desc.constants[ShaderStage::Vertex].byteCount = 0;
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(InverseGammaPixelRC);
		desc.samplerCount = 1;
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, 1);
		desc.genericVisibility = ShaderStages::PixelBit;
		inverseToneMapRootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc("inverse tone map", inverseToneMapRootSignature);
		inverseToneMapDescriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update;
		update.SetSamplers(1, &grp.samplers[GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear)]);
		UpdateDescriptorTable(inverseToneMapDescriptorTable, update);
	}
	{
		GraphicsPipelineDesc desc("inverse tone map", inverseToneMapRootSignature);
		desc.vertexShader = ShaderByteCode(inverse_tone_map::g_vs);
		desc.pixelShader = ShaderByteCode(inverse_tone_map::g_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_TWO_SIDED;
		desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
		inverseToneMapPipeline = CreateGraphicsPipeline(desc);
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
	UpdateDescriptorTable(toneMapDescriptorTable, update);

	// @TODO: r_blitMode support
	GammaVertexRC vertexRC = {};
	vertexRC.scaleX = 1.0f;
	vertexRC.scaleY = 1.0f;

	GammaPixelRC pixelRC = {};
	pixelRC.invGamma = 1.0f / r_gamma->value;
	pixelRC.brightness = r_brightness->value;
	pixelRC.greyscale = r_greyscale->value;

	CmdSetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdBindRenderTargets(1, &swapChain, NULL);
	CmdBindPipeline(toneMapPipeline);
	CmdBindRootSignature(toneMapRootSignature);
	CmdBindDescriptorTable(toneMapRootSignature, toneMapDescriptorTable);
	CmdSetRootConstants(toneMapRootSignature, ShaderStage::Vertex, &vertexRC);
	CmdSetRootConstants(toneMapRootSignature, ShaderStage::Pixel, &pixelRC);
	CmdDraw(3, 0);
}

void PostProcess::ToneMap(HTexture texture)
{
	DescriptorTableUpdate update;
	update.SetTextures(1, &texture);
	UpdateDescriptorTable(toneMapDescriptorTable, update);

	GammaVertexRC vertexRC = {};
	vertexRC.scaleX = 1.0f;
	vertexRC.scaleY = 1.0f;

	GammaPixelRC pixelRC = {};
	pixelRC.invGamma = 1.0f / r_gamma->value;
	pixelRC.brightness = r_brightness->value;
	pixelRC.greyscale = 0.0f;

	CmdBindPipeline(toneMapPipeline);
	CmdBindRootSignature(toneMapRootSignature);
	CmdBindDescriptorTable(toneMapRootSignature, toneMapDescriptorTable);
	CmdSetRootConstants(toneMapRootSignature, ShaderStage::Vertex, &vertexRC);
	CmdSetRootConstants(toneMapRootSignature, ShaderStage::Pixel, &pixelRC);
	CmdDraw(3, 0);
}

void PostProcess::InverseToneMap(HTexture texture)
{
	DescriptorTableUpdate update;
	update.SetTextures(1, &texture);
	UpdateDescriptorTable(inverseToneMapDescriptorTable, update);

	InverseGammaPixelRC pixelRC = {};
	pixelRC.gamma = r_gamma->value;
	pixelRC.invBrightness = 1.0f / r_brightness->value;

	CmdBindPipeline(inverseToneMapPipeline);
	CmdBindRootSignature(inverseToneMapRootSignature);
	CmdBindDescriptorTable(inverseToneMapRootSignature, inverseToneMapDescriptorTable);
	CmdSetRootConstants(inverseToneMapRootSignature, ShaderStage::Pixel, &pixelRC);
	CmdDraw(3, 0);
}
