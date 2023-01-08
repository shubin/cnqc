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
// Gameplay Rendering Pipeline - Dear ImGUI integration


#include "grp_local.h"
#include "../imgui/imgui.h"


#define MAX_VERTEX_COUNT (64 << 10)
#define MAX_INDEX_COUNT  (MAX_VERTEX_COUNT << 3)


static const char* vs = R"grml(
cbuffer vertexBuffer : register(b0)
{
	float4x4 ProjectionMatrix;
};

struct VS_INPUT
{
	float2 pos : POSITION;
	float4 col : COLOR0;
	float2 uv  : TEXCOORD0;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
	float2 uv  : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
	PS_INPUT output;
	output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0, 1.0));
	output.col = input.col;
	output.uv  = input.uv;
	return output;
}
)grml";

static const char* ps = R"grml(
struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float4 col : COLOR0;
	float2 uv  : TEXCOORD0;
};

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
	float4 out_col = input.col * texture0.Sample(sampler0, input.uv);
	return out_col;
}
)grml";


struct VERTEX_CONSTANT_BUFFER_DX12
{
	float mvp[4][4];
};

struct ImGui_ImplDX12_RenderBuffers
{
	HBuffer IndexBuffer;
	HBuffer VertexBuffer;
};

struct ImGui_ImplDX12_Data
{
	HRootSignature pRootSignature;
	HPipeline pPipelineState;
	HTexture pFontTextureResource;

	ImGui_ImplDX12_RenderBuffers pFrameResources[FrameCount];
};

static ImGui_ImplDX12_Data bd;

HSampler sampler;
HRootSignature rootSignature;
HPipeline pipeline;
HDescriptorTable descriptorTable;
HTexture fontAtlas;


void imgui_t::Init()
{
	ImGuiIO& io = ImGui::GetIO();
	if(io.BackendRendererUserData != NULL)
	{
		return;
	}

	io.BackendRendererUserData = &bd;
	io.BackendRendererName = "CNQ3 Direct3D 12";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

	for(int i = 0; i < FrameCount; i++)
	{
		ImGui_ImplDX12_RenderBuffers* fr = &bd.pFrameResources[i];

		BufferDesc desc = { 0 };
		desc.committedResource = true;
		desc.memoryUsage = MemoryUsage::Upload;

		desc.name = "Dear ImGUI index buffer";
		desc.initialState = ResourceState::IndexBufferBit;
		desc.byteCount = MAX_INDEX_COUNT * sizeof(ImDrawIdx);
		fr->IndexBuffer = CreateBuffer(desc);

		desc.name = "Dear ImGUI vertex buffer";
		desc.initialState = ResourceState::VertexBufferBit;
		desc.byteCount = MAX_VERTEX_COUNT * sizeof(ImDrawData);
		fr->VertexBuffer = CreateBuffer(desc);
	}

	{
		// @TODO: BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK
		SamplerDesc desc = {};
		desc.wrapMode = TW_REPEAT;
		desc.filterMode = TextureFilter::Linear;
		sampler = CreateSampler(desc);
	}

	{
		RootSignatureDesc desc = { 0 };
		desc.name = "Dear ImGUI root signature";
		desc.pipelineType = PipelineType::Graphics;
		desc.usingVertexBuffers = true;
		desc.samplerCount = 1;
		desc.samplerVisibility = ShaderStage::PixelBit;
		desc.genericVisibility = ShaderStage::PixelBit;
		desc.constants[ShaderType::Vertex].count = 16;
		desc.AddRange(DescriptorType::Texture, 0, 1);
		rootSignature = CreateRootSignature(desc);
	}

	{
		GraphicsPipelineDesc desc = { 0 };
		desc.name = "Dear ImGUI PSO";
		desc.rootSignature = rootSignature;
		desc.vertexShader = CompileVertexShader(vs);
		desc.pixelShader = CompilePixelShader(ps);
		desc.vertexLayout.bindingStrides[0] = sizeof(ImDrawVert);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position,
			DataType::Float32, 2, offsetof(ImDrawVert, pos));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::TexCoord,
			DataType::Float32, 2, offsetof(ImDrawVert, uv));
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Color,
			DataType::UNorm8, 4, offsetof(ImDrawVert, col));
		desc.depthStencil.depthComparison = ComparisonFunction::Always;
		desc.depthStencil.depthStencilFormat = TextureFormat::DepthStencil32_UNorm24_UInt8;
		desc.depthStencil.enableDepthTest = false;
		desc.depthStencil.enableDepthWrites = false;
		desc.rasterizer.cullMode = CullMode::None;
		desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}

	{
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		TextureDesc desc = { 0 };
		desc.name = "Dear ImGUI font atlas";
		desc.initialState = ResourceState::PixelShaderAccessBit;
		desc.allowedState = ResourceState::PixelShaderAccessBit;
		desc.width = width;
		desc.height = height;
		desc.mipCount = 1;
		desc.sampleCount = 1;
		desc.format = TextureFormat::RGBA32_UNorm;
		desc.committedResource = true;
		fontAtlas = CreateTexture(desc);

		TextureUpload upload = { 0 };
		upload.width = width;
		upload.height = height;
		upload.data = pixels;
		UploadTextureMip0(fontAtlas, upload);
	}

	{
		// @TODO: use the same big shared descriptor table for SRVs/samplers
		// as the rest of the GRP?

		DescriptorTableDesc desc = { 0 };
		desc.name = "Dear ImGUI descriptor table";
		desc.rootSignature = rootSignature;
		descriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update = { 0 };
		update.resourceCount = 1;

		update.type = DescriptorType::Sampler;
		update.samplers = &sampler;
		UpdateDescriptorTable(descriptorTable, update);

		update.type = DescriptorType::Texture;
		update.textures = &fontAtlas;
		UpdateDescriptorTable(descriptorTable, update);
	}
}

void imgui_t::Draw()
{
	if(r_debugUI->integer == 0)
	{
		return;
	}

	grp.projection = PROJECTION_IMGUI;

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize.x = glConfig.vidWidth;
	io.DisplaySize.y = glConfig.vidHeight;

	ImGui::NewFrame();

	// @TODO:
	//DrawGUI();
	ImGui::ShowDemoWindow();

	ImGui::EndFrame();
	ImGui::Render();

	const ImDrawData* draw_data = ImGui::GetDrawData();

	// avoid rendering when minimized
	if(draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
	{
		return;
	}

	ImGui_ImplDX12_RenderBuffers* fr = &bd.pFrameResources[GetFrameIndex()];

	// Upload vertex/index data into a single contiguous GPU buffer
	void* vtx_resource = MapBuffer(fr->VertexBuffer);
	void* idx_resource = MapBuffer(fr->IndexBuffer);
	ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource;
	ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource;
	for(int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}
	UnmapBuffer(fr->VertexBuffer);
	UnmapBuffer(fr->IndexBuffer);

	// Setup orthographic projection matrix into our constant buffer
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
	VERTEX_CONSTANT_BUFFER_DX12 vertex_constant_buffer;
	{
		float L = draw_data->DisplayPos.x;
		float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
		float T = draw_data->DisplayPos.y;
		float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
		float mvp[4][4] =
		{
			{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
			{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
			{ 0.0f,         0.0f,           0.5f,       0.0f },
			{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
		};
		memcpy(&vertex_constant_buffer.mvp, mvp, sizeof(mvp));
	}

	const uint32_t vertexStride = sizeof(ImDrawVert);
	static_assert(sizeof(ImDrawIdx) == 4, "uint32 indices expected!");

	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, descriptorTable);
	CmdBindVertexBuffers(1, &fr->VertexBuffer, &vertexStride, NULL);
	CmdBindIndexBuffer(fr->IndexBuffer, IndexType::UInt32, 0);
	CmdSetViewport(0, 0, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
	CmdSetRootConstants(rootSignature, ShaderType::Vertex, &vertex_constant_buffer);

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	int global_vtx_offset = 0;
	int global_idx_offset = 0;
	ImVec2 clip_off = draw_data->DisplayPos;
	for(int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		for(int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

			// Project scissor/clipping rectangles into framebuffer space
			ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
			ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
			if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
				continue;

			// Apply Scissor/clipping rectangle, Draw
			CmdSetScissor(clip_min.x, clip_min.y, clip_max.x - clip_min.x, clip_max.y - clip_min.y);
			CmdDrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
		}

		global_idx_offset += cmd_list->IdxBuffer.Size;
		global_vtx_offset += cmd_list->VtxBuffer.Size;
	}
}
