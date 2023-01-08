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
	int IndexBufferSize;
	int VertexBufferSize;
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



static void CreateFontAtlas()
{
	ImGuiIO& io = ImGui::GetIO();
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
	HTexture fontAtlas = CreateTexture(desc);

	TextureUpload upload = { 0 };
	upload.width = width;
	upload.height = height;
	upload.data = pixels;
	UploadTextureMip0(fontAtlas, upload);

	io.Fonts->SetTexID((ImTextureID)fontAtlas.v);
}


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
		fr->IndexBufferSize = 10000;
		fr->VertexBufferSize = 5000;

		BufferDesc desc = { 0 };
		desc.committedResource = true;
		desc.memoryUsage = MemoryUsage::Upload;

		desc.name = "Dear ImGUI index buffer";
		desc.initialState = ResourceState::IndexBufferBit;
		desc.byteCount = fr->IndexBufferSize;
		fr->IndexBuffer = CreateBuffer(desc);

		desc.name = "Dear ImGUI vertex buffer";
		desc.initialState = ResourceState::VertexBufferBit;
		desc.byteCount = fr->VertexBufferSize;
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

	CreateFontAtlas();
}

void imgui_t::Draw()
{
	if(r_debugUI->integer)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize.x = glConfig.vidWidth;
	io.DisplaySize.y = glConfig.vidHeight;

	// @TODO:
	//ImGui_ImplDX12_NewFrame();

	ImGui::NewFrame();

	// @TODO:
	//DrawGUI();
	ImGui::ShowAboutWindow();

	ImGui::EndFrame();
	ImGui::Render();

	// @TODO:
	//ImDrawData* const data = ImGui::GetDrawData();
}
