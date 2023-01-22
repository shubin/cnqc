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


#pragma pack(push, 1)

struct PostVertexRC
{
	float scaleX;
	float scaleY;
};

#pragma pack(pop)

static const char* post_vs = R"grml(
cbuffer RootConstants
{
	float scaleX;
	float scaleY;
};

struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
};

VOut main(uint id : SV_VertexID)
{
	VOut output;
	output.position.x = scaleX * ((float)(id / 2) * 4.0 - 1.0);
	output.position.y = scaleY * ((float)(id % 2) * 4.0 - 1.0);
	output.position.z = 0.0;
	output.position.w = 1.0;
	output.texCoords.x = (float)(id / 2) * 2.0;
	output.texCoords.y = 1.0 - (float)(id % 2) * 2.0;

	return output;
}
)grml";

#pragma pack(push, 1)

struct PostPixelRC
{
	float invGamma;
	float brightness;
	float greyscale;
};

#pragma pack(pop)

static const char* post_ps = R"grml(
// X3571: pow(f, e) won't work if f is negative
#pragma warning(disable : 3571)

cbuffer RootConstants
{
	float invGamma;
	float brightness;
	float greyscale;
};

struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 MakeGreyscale(float4 input, float amount)
{
	float grey = dot(input.rgb, float3(0.299, 0.587, 0.114));
	float4 result = lerp(input, float4(grey, grey, grey, input.a), amount);

	return result;
}

float4 main(VOut input) : SV_TARGET
{
	float3 base = texture0.Sample(sampler0, input.texCoords).rgb;
	float3 gc = pow(base, invGamma) * brightness;

	return MakeGreyscale(float4(gc.rgb, 1.0), greyscale);
}
)grml";


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
		desc.vertexShader = CompileVertexShader(post_vs);
		desc.pixelShader = CompilePixelShader(post_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_TWO_SIDED;
		desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}
}

void PostProcess::Draw()
{
	grp.BeginRenderPass("Post-process", 0.0f, 0.0f, 0.1f);

	const HTexture swapChain = GetSwapChainTexture();
	const TextureBarrier barries[2] =
	{
		TextureBarrier(grp.renderTarget, ResourceStates::PixelShaderAccessBit),
		TextureBarrier(swapChain, ResourceStates::RenderTargetBit)
	};
	CmdBarrier(ARRAY_LEN(barries), barries);

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

	grp.EndRenderPass();
}
