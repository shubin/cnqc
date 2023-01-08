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


static void ImGui_ImplDX12_CreateFontsTexture()
{
#if 0
	// Build texture atlas
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	{
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Alignment = 0;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		ID3D12Resource* pTexture = nullptr;
		bd->pd3dDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pTexture));

		UINT uploadPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		UINT uploadSize = height * uploadPitch;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = uploadSize;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		ID3D12Resource* uploadBuffer = nullptr;
		HRESULT hr = bd->pd3dDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
		IM_ASSERT(SUCCEEDED(hr));

		void* mapped = nullptr;
		D3D12_RANGE range = { 0, uploadSize };
		hr = uploadBuffer->Map(0, &range, &mapped);
		IM_ASSERT(SUCCEEDED(hr));
		for(int y = 0; y < height; y++)
			memcpy((void*)((uintptr_t)mapped + y * uploadPitch), pixels + y * width * 4, width * 4);
		uploadBuffer->Unmap(0, &range);

		D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
		srcLocation.pResource = uploadBuffer;
		srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srcLocation.PlacedFootprint.Footprint.Width = width;
		srcLocation.PlacedFootprint.Footprint.Height = height;
		srcLocation.PlacedFootprint.Footprint.Depth = 1;
		srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

		D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
		dstLocation.pResource = pTexture;
		dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLocation.SubresourceIndex = 0;

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = pTexture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		ID3D12Fence* fence = nullptr;
		hr = bd->pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		IM_ASSERT(SUCCEEDED(hr));

		HANDLE event = CreateEvent(0, 0, 0, 0);
		IM_ASSERT(event != nullptr);

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 1;

		ID3D12CommandQueue* cmdQueue = nullptr;
		hr = bd->pd3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
		IM_ASSERT(SUCCEEDED(hr));

		ID3D12CommandAllocator* cmdAlloc = nullptr;
		hr = bd->pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
		IM_ASSERT(SUCCEEDED(hr));

		ID3D12GraphicsCommandList* cmdList = nullptr;
		hr = bd->pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr, IID_PPV_ARGS(&cmdList));
		IM_ASSERT(SUCCEEDED(hr));

		cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
		cmdList->ResourceBarrier(1, &barrier);

		hr = cmdList->Close();
		IM_ASSERT(SUCCEEDED(hr));

		cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdList);
		hr = cmdQueue->Signal(fence, 1);
		IM_ASSERT(SUCCEEDED(hr));

		fence->SetEventOnCompletion(1, event);
		WaitForSingleObject(event, INFINITE);

		cmdList->Release();
		cmdAlloc->Release();
		cmdQueue->Release();
		CloseHandle(event);
		fence->Release();
		uploadBuffer->Release();

		// Create texture view
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		bd->pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, bd->hFontSrvCpuDescHandle);
		SafeRelease(bd->pFontTextureResource);
		bd->pFontTextureResource = pTexture;
	}

	// Store our identifier
	// READ THIS IF THE STATIC_ASSERT() TRIGGERS:
	// - Important: to compile on 32-bit systems, this backend requires code to be compiled with '#define ImTextureID ImU64'.
	// - This is because we need ImTextureID to carry a 64-bit value and by default ImTextureID is defined as void*.
	// [Solution 1] IDE/msbuild: in "Properties/C++/Preprocessor Definitions" add 'ImTextureID=ImU64' (this is what we do in the 'example_win32_direct12/example_win32_direct12.vcxproj' project file)
	// [Solution 2] IDE/msbuild: in "Properties/C++/Preprocessor Definitions" add 'IMGUI_USER_CONFIG="my_imgui_config.h"' and inside 'my_imgui_config.h' add '#define ImTextureID ImU64' and as many other options as you like.
	// [Solution 3] IDE/msbuild: edit imconfig.h and add '#define ImTextureID ImU64' (prefer solution 2 to create your own config file!)
	// [Solution 4] command-line: add '/D ImTextureID=ImU64' to your cl.exe command-line (this is what we do in the example_win32_direct12/build_win32.bat file)
	static_assert(sizeof(ImTextureID) >= sizeof(bd->hFontSrvGpuDescHandle.ptr), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
	io.Fonts->SetTexID((ImTextureID)bd->hFontSrvGpuDescHandle.ptr);
#endif
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

	ImGui_ImplDX12_CreateFontsTexture();
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
