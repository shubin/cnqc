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
// Gameplay Rendering Pipeline - UI/2D rendering


#include "grp_local.h"


#pragma pack(push, 4)
struct VertexRC
{
	float scale[2];
};

struct PixelRC
{
	uint32_t texture;
	uint32_t sampler;
};
#pragma pack(pop)

static const char* vs = R"grml(
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

static const char* ps = R"grml(
cbuffer RootConstants
{
	uint textureIndex;
	uint samplerIndex;
};

Texture2D textures2D[4096] : register(t0);
SamplerState samplers[12] : register(s0);

struct VOut
{
	float4 position : SV_Position;
	float2 texCoords : TEXCOORD0;
	float4 color : COLOR0;
};

float4 main(VOut input) : SV_Target
{
	return textures2D[textureIndex].Sample(samplers[samplerIndex], input.texCoords) * input.color;
}
)grml";


void UI::Init()
{
	if(!grp.firstInit)
	{
		return;
	}

	{
		RootSignatureDesc desc = grp.rootSignatureDesc;
		desc.name = "UI";
		desc.constants[ShaderStage::Vertex].byteCount = 8;
		desc.constants[ShaderStage::Pixel].byteCount = 8;
		rootSignature = CreateRootSignature(desc);
	}
	{
		GraphicsPipelineDesc desc("UI", rootSignature);
		desc.vertexShader = CompileVertexShader(vs);
		desc.pixelShader = CompilePixelShader(ps);
		desc.vertexLayout.bindingStrides[0] = sizeof(UI::Vertex);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position,
			DataType::Float32, 2, offsetof(UI::Vertex, position));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::TexCoord,
			DataType::Float32, 2, offsetof(UI::Vertex, texCoords));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Color,
			DataType::UNorm8, 4, offsetof(UI::Vertex, color));
		desc.depthStencil.depthComparison = ComparisonFunction::Always;
		desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
		desc.depthStencil.enableDepthTest = false;
		desc.depthStencil.enableDepthWrites = false;
		desc.rasterizer.cullMode = CT_TWO_SIDED;
		desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}
	maxVertexCount = 640 << 10;
	maxIndexCount = 8 * maxVertexCount;
	{
		BufferDesc desc("UI index", sizeof(UI::Index) * maxIndexCount * FrameCount, ResourceStates::IndexBufferBit);
		desc.memoryUsage = MemoryUsage::Upload;
		indexBuffer = CreateBuffer(desc);
		indices = (UI::Index*)MapBuffer(indexBuffer);
	}
	{
		BufferDesc desc("UI vertex", sizeof(UI::Vertex) * maxVertexCount * FrameCount, ResourceStates::VertexBufferBit);
		desc.memoryUsage = MemoryUsage::Upload;
		vertexBuffer = CreateBuffer(desc);
		vertices = (UI::Vertex*)MapBuffer(vertexBuffer);
	}
}

void UI::BeginFrame()
{
	// move to this frame's dedicated buffer section
	const uint32_t frameIndex = GetFrameIndex();
	firstIndex = frameIndex * maxIndexCount;
	firstVertex = frameIndex * maxVertexCount;
}

void UI::Begin()
{
	if(grp.renderMode == RenderMode::UI)
	{
		return;
	}

	grp.BeginRenderPass("UI", 0.0f, 0.85f, 1.0f);

	CmdBindRenderTargets(1, &grp.renderTarget, NULL);

	// UI always uses the entire render surface
	CmdSetViewportAndScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);

	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, grp.descriptorTable);
	const uint32_t stride = sizeof(UI::Vertex);
	CmdBindVertexBuffers(1, &vertexBuffer, &stride, NULL);
	CmdBindIndexBuffer(indexBuffer, indexType, 0);

	VertexRC vertexRC = {};
	vertexRC.scale[0] = 2.0f / glConfig.vidWidth;
	vertexRC.scale[1] = 2.0f / glConfig.vidHeight;
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, &vertexRC);

	grp.renderMode = RenderMode::UI;
}

void UI::DrawBatch()
{
	if(indexCount <= 0)
	{
		return;
	}

	Begin();

	// @TODO: support for custom shaders?
	PixelRC pixelRC = {};
	pixelRC.texture = GetBundleImage(shader->stages[0]->bundle)->textureIndex;
	pixelRC.sampler = GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear);
	CmdSetRootConstants(rootSignature, ShaderStage::Pixel, &pixelRC);

	CmdDrawIndexed(indexCount, firstIndex, 0);
	firstIndex += indexCount;
	firstVertex += vertexCount;
	indexCount = 0;
	vertexCount = 0;
}

void UI::UISetColor(const uiSetColorCommand_t& cmd)
{
	byte* const colors = (byte*)&color;
	colors[0] = (byte)(cmd.color[0] * 255.0f);
	colors[1] = (byte)(cmd.color[1] * 255.0f);
	colors[2] = (byte)(cmd.color[2] * 255.0f);
	colors[3] = (byte)(cmd.color[3] * 255.0f);
}

void UI::UIDrawQuad(const uiDrawQuadCommand_t& cmd)
{
	if(vertexCount + 4 > maxVertexCount ||
		indexCount + 6 > maxIndexCount)
	{
		return;
	}

	Begin();

	if(shader != cmd.shader)
	{
		DrawBatch();
	}

	shader = cmd.shader;

	const int v = firstVertex + vertexCount;
	const int i = firstIndex + indexCount;
	vertexCount += 4;
	indexCount += 6;

	indices[i + 0] = v + 3;
	indices[i + 1] = v + 0;
	indices[i + 2] = v + 2;
	indices[i + 3] = v + 2;
	indices[i + 4] = v + 0;
	indices[i + 5] = v + 1;

	vertices[v + 0].position[0] = cmd.x;
	vertices[v + 0].position[1] = cmd.y;
	vertices[v + 0].texCoords[0] = cmd.s1;
	vertices[v + 0].texCoords[1] = cmd.t1;
	vertices[v + 0].color = color;

	vertices[v + 1].position[0] = cmd.x + cmd.w;
	vertices[v + 1].position[1] = cmd.y;
	vertices[v + 1].texCoords[0] = cmd.s2;
	vertices[v + 1].texCoords[1] = cmd.t1;
	vertices[v + 1].color = color;

	vertices[v + 2].position[0] = cmd.x + cmd.w;
	vertices[v + 2].position[1] = cmd.y + cmd.h;
	vertices[v + 2].texCoords[0] = cmd.s2;
	vertices[v + 2].texCoords[1] = cmd.t2;
	vertices[v + 2].color = color;

	vertices[v + 3].position[0] = cmd.x;
	vertices[v + 3].position[1] = cmd.y + cmd.h;
	vertices[v + 3].texCoords[0] = cmd.s1;
	vertices[v + 3].texCoords[1] = cmd.t2;
	vertices[v + 3].color = color;
}

void UI::UIDrawTriangle(const uiDrawTriangleCommand_t& cmd)
{
	if(vertexCount + 3 > maxVertexCount ||
		indexCount + 3 > maxIndexCount)
	{
		return;
	}

	Begin();

	if(shader != cmd.shader)
	{
		DrawBatch();
	}

	shader = cmd.shader;

	const int v = firstVertex + vertexCount;
	const int i = firstIndex + indexCount;
	vertexCount += 3;
	indexCount += 3;

	indices[i + 0] = v + 0;
	indices[i + 1] = v + 1;
	indices[i + 2] = v + 2;

	vertices[v + 0].position[0] = cmd.x0;
	vertices[v + 0].position[1] = cmd.y0;
	vertices[v + 0].texCoords[0] = cmd.s0;
	vertices[v + 0].texCoords[1] = cmd.t0;
	vertices[v + 0].color = color;

	vertices[v + 1].position[0] = cmd.x1;
	vertices[v + 1].position[1] = cmd.y1;
	vertices[v + 1].texCoords[0] = cmd.s1;
	vertices[v + 1].texCoords[1] = cmd.t1;
	vertices[v + 1].color = color;

	vertices[v + 2].position[0] = cmd.x2;
	vertices[v + 2].position[1] = cmd.y2;
	vertices[v + 2].texCoords[0] = cmd.s2;
	vertices[v + 2].texCoords[1] = cmd.t2;
	vertices[v + 2].color = color;
}
