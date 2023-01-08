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


void ui_t::Init()
{
	{
		RootSignatureDesc desc;
		desc.name = "UI root signature";
		desc.usingVertexBuffers = qtrue;
		desc.constants[ShaderStage::Vertex].byteCount = 8;
		desc.constants[ShaderStage::Pixel].byteCount = 8;
		desc.samplerCount = ARRAY_LEN(grp.samplers);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, MAX_DRAWIMAGES);
		rootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc = { 0 };
		desc.name = "UI descriptor table";
		desc.rootSignature = rootSignature;
		descriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update = { 0 };
		update.type = DescriptorType::Sampler;
		update.firstIndex = 0;
		update.resourceCount = ARRAY_LEN(grp.samplers);
		update.samplers = grp.samplers;
		UpdateDescriptorTable(descriptorTable, update);
	}
	{
		GraphicsPipelineDesc desc = { 0 };
		desc.name = "UI PSO";
		desc.rootSignature = rootSignature;
		desc.vertexShader = CompileVertexShader(vs);
		desc.pixelShader = CompilePixelShader(ps);
		desc.vertexLayout.bindingStrides[0] = sizeof(ui_t::vertex_t);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position,
			DataType::Float32, 2, offsetof(ui_t::vertex_t, position));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::TexCoord,
			DataType::Float32, 2, offsetof(ui_t::vertex_t, texCoords));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Color,
			DataType::UNorm8, 4, offsetof(ui_t::vertex_t, color));
		desc.depthStencil.depthComparison = ComparisonFunction::Always;
		desc.depthStencil.depthStencilFormat = TextureFormat::DepthStencil32_UNorm24_UInt8;
		desc.depthStencil.enableDepthTest = false;
		desc.depthStencil.enableDepthWrites = false;
		desc.rasterizer.cullMode = CullMode::None;
		desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}
	maxVertexCount = 64 << 10;
	maxIndexCount = 8 * maxVertexCount;
	{
		BufferDesc desc = { 0 };
		desc.name = "UI index buffer";
		desc.byteCount = sizeof(ui_t::index_t) * maxIndexCount * FrameCount;
		desc.memoryUsage = MemoryUsage::Upload;
		desc.initialState = ResourceStates::IndexBufferBit;
		indexBuffer = CreateBuffer(desc);
		indices = (ui_t::index_t*)MapBuffer(indexBuffer);

	}
	{
		BufferDesc desc = { 0 };
		desc.name = "UI vertex buffer";
		desc.byteCount = sizeof(ui_t::vertex_t) * maxVertexCount * FrameCount;
		desc.memoryUsage = MemoryUsage::Upload;
		desc.initialState = ResourceStates::VertexBufferBit;
		vertexBuffer = CreateBuffer(desc);
		vertices = (ui_t::vertex_t*)MapBuffer(vertexBuffer);
	}
}

void ui_t::BeginFrame()
{
	// move to this frame's dedicated buffer section
	const uint32_t frameIndex = GetFrameIndex();
	firstIndex = frameIndex * maxIndexCount;
	firstVertex = frameIndex * maxVertexCount;
}

void ui_t::Begin()
{
	if(grp.projection == PROJECTION_2D)
	{
		return;
	}

	// @TODO: grab the right rects...
	CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, descriptorTable);
	const uint32_t stride = sizeof(ui_t::vertex_t);
	CmdBindVertexBuffers(1, &vertexBuffer, &stride, NULL);
	CmdBindIndexBuffer(indexBuffer, IndexType::UInt32, 0);
	const float scale[2] = { 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight };
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, scale);

	grp.projection = PROJECTION_2D;
}

void ui_t::Draw()
{
	if(indexCount <= 0)
	{
		return;
	}

	const uint32_t textureIndex = shader->stages[0]->bundle.image[0]->textureIndex;
	const uint32_t pixelConstants[2] = { textureIndex, 0 }; // second one is the sampler index
	CmdSetRootConstants(rootSignature, ShaderStage::Pixel, pixelConstants);
	CmdDrawIndexed(indexCount, firstIndex, 0);
	firstIndex += indexCount;
	firstVertex += vertexCount;
	indexCount = 0;
	vertexCount = 0;
}

const void* ui_t::SetColor(const void* data)
{
	const setColorCommand_t* cmd = (const setColorCommand_t*)data;

	byte* const colors = (byte*)&color;
	colors[0] = (byte)(cmd->color[0] * 255.0f);
	colors[1] = (byte)(cmd->color[1] * 255.0f);
	colors[2] = (byte)(cmd->color[2] * 255.0f);
	colors[3] = (byte)(cmd->color[3] * 255.0f);

	return (const void*)(cmd + 1);
}

const void* ui_t::StretchPic(const void* data)
{
	const stretchPicCommand_t* cmd = (const stretchPicCommand_t*)data;

	if(vertexCount + 4 > maxVertexCount ||
		indexCount + 6 > maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin();

	if(shader != cmd->shader)
	{
		Draw();
	}

	shader = cmd->shader;

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

	vertices[v + 0].position[0] = cmd->x;
	vertices[v + 0].position[1] = cmd->y;
	vertices[v + 0].texCoords[0] = cmd->s1;
	vertices[v + 0].texCoords[1] = cmd->t1;
	vertices[v + 0].color = color;

	vertices[v + 1].position[0] = cmd->x + cmd->w;
	vertices[v + 1].position[1] = cmd->y;
	vertices[v + 1].texCoords[0] = cmd->s2;
	vertices[v + 1].texCoords[1] = cmd->t1;
	vertices[v + 1].color = color;

	vertices[v + 2].position[0] = cmd->x + cmd->w;
	vertices[v + 2].position[1] = cmd->y + cmd->h;
	vertices[v + 2].texCoords[0] = cmd->s2;
	vertices[v + 2].texCoords[1] = cmd->t2;
	vertices[v + 2].color = color;

	vertices[v + 3].position[0] = cmd->x;
	vertices[v + 3].position[1] = cmd->y + cmd->h;
	vertices[v + 3].texCoords[0] = cmd->s1;
	vertices[v + 3].texCoords[1] = cmd->t2;
	vertices[v + 3].color = color;

	return (const void*)(cmd + 1);
}

const void* ui_t::Triangle(const void* data)
{
	const triangleCommand_t* cmd = (const triangleCommand_t*)data;

	if(vertexCount + 3 > maxVertexCount ||
		indexCount + 3 > maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin();

	if(shader != cmd->shader)
	{
		Draw();
	}

	shader = cmd->shader;

	const int v = firstVertex + vertexCount;
	const int i = firstIndex + indexCount;
	vertexCount += 3;
	indexCount += 3;

	indices[i + 0] = v + 0;
	indices[i + 1] = v + 1;
	indices[i + 2] = v + 2;

	vertices[v + 0].position[0] = cmd->x0;
	vertices[v + 0].position[1] = cmd->y0;
	vertices[v + 0].texCoords[0] = cmd->s0;
	vertices[v + 0].texCoords[1] = cmd->t0;
	vertices[v + 0].color = color;

	vertices[v + 1].position[0] = cmd->x1;
	vertices[v + 1].position[1] = cmd->y1;
	vertices[v + 1].texCoords[0] = cmd->s1;
	vertices[v + 1].texCoords[1] = cmd->t1;
	vertices[v + 1].color = color;

	vertices[v + 2].position[0] = cmd->x2;
	vertices[v + 2].position[1] = cmd->y2;
	vertices[v + 2].texCoords[0] = cmd->s2;
	vertices[v + 2].texCoords[1] = cmd->t2;
	vertices[v + 2].color = color;

	return (const void*)(cmd + 1);
}
