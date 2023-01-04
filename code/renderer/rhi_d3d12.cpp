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
// Direct3D 12 Rendering Hardware Interface


/*
to do:

- get rid of d3dx12.h
* partial inits and shutdown
- move the Dear ImGui rendering outside of the RHI
* integrate D3D12MA
- D3D12MA: leverage rhi.allocator->IsUMA() & rhi.allocator->IsCacheCoherentUMA()
- D3D12MA: defragment on partial inits?
- compiler switch for GPU validation
- use ID3D12Device4::CreateCommandList1 to create closed command lists
- if a feature level below 12.0 is good enough,
	use ID3D12Device::CheckFeatureSupport with D3D12_FEATURE_D3D12_OPTIONS / D3D12_FEATURE_DATA_D3D12_OPTIONS
	to ensure Resource Binding Tier 2 is available
- IDXGISwapChain::SetFullScreenState(TRUE) with the borderless window taking up the entire screen
	and ALLOW_TEARING set on both the flip mode swap chain and Present() flags
	will enable true immediate independent flip mode and give us the lowest latency possible
- NvAPI_D3D_GetLatency to get (simulated) input to display latency
- NvAPI_D3D_IsGSyncCapable / NvAPI_D3D_IsGSyncActive for diagnostics
- CVar for setting the gpuPreference_t
*/

/*
All three types of command list use the ID3D12GraphicsCommandList interface,
however only a subset of the methods are supported for copy and compute.

Copy and compute command lists can use the following methods:
Close
CopyBufferRegion
CopyResource
CopyTextureRegion
CopyTiles
Reset
ResourceBarrier

Compute command lists can also use the following methods:
ClearState
ClearUnorderedAccessViewFloat
ClearUnorderedAccessViewUint
DiscardResource
Dispatch
ExecuteIndirect
SetComputeRoot32BitConstant
SetComputeRoot32BitConstants
SetComputeRootConstantBufferView
SetComputeRootDescriptorTable
SetComputeRootShaderResourceView
SetComputeRootSignature
SetComputeRootUnorderedAccessView
SetDescriptorHeaps
SetPipelineState
SetPredication
EndQuery
*/


#include "tr_local.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
//#include <d3dx12.h>
#if defined(_DEBUG) || defined(CNQ3_DEV)
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")
#endif
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include "D3D12MemAlloc.h"
// @TODO: move out of the RHI...
#include "../imgui/imgui_impl_dx12.h"


#if defined(_DEBUG) || defined(CNQ3_DEV)
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }
#endif


namespace RHI
{
	// D3D_FEATURE_LEVEL_12_0 is the minimum to ensure at least Resource Binding Tier 2:
	// - unlimited SRVs
	// - 14 CBVs
	// - 64 UAVs
	// - 2048 samplers
	static const D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_12_0;

	struct ResourceType
	{
		enum Id
		{
			// @NOTE: a valid type never being 0 means we can discard 0 handles right away
			Invalid,
			// public stuff
			Fence,
			Buffer,
			Texture,
			RootSignature,
			Pipeline,
			DurationQuery,
			// private stuff
			//
			Count
		};
	};

	struct PipelineType
	{
		enum Id
		{
			Graphics,
			Compute,
			Count
		};
	};

	struct Fence
	{
		ID3D12Fence* fence;
		HANDLE fenceEvent;
		UINT64 fenceValue;
	};

	struct Buffer
	{
		BufferDesc desc;
		D3D12MA::Allocation* allocation;
		ID3D12Resource* buffer;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
		bool mapped;
	};

	struct Texture
	{
		TextureDesc desc;
		D3D12MA::Allocation* allocation;
		ID3D12Resource* texture;
		uint32_t srvIndex;
		struct SubResource
		{
			ResourceState::Flags state;
			// additional views here?
		}
		subResources[16];
	};

	struct RootSignature
	{
		struct PerStageConstants
		{
			UINT parameterIndex;
		};
		RootSignatureDesc desc;
		ID3D12RootSignature* signature;
		PerStageConstants constants[ShaderType::Count];
		UINT firstTableIndex;
	};

	struct Pipeline
	{
		union
		{
			GraphicsPipelineDesc graphicsDesc;
		};
		ID3D12PipelineState* pso;
		PipelineType::Id type;
	};

	struct QueryState
	{
		enum Id
		{
			Free,  // ready to be (re-)used
			Begun, // first  call done, not resolved yet
			Ended, // second call done, not resolved yet
			Count
		};
	};

	/*struct Queue
	{
		void Create();
		void Release();

		ID3D12CommandQueue* commandQueue;
		ID3D12CommandAllocator* commandAllocator;
		ID3D12GraphicsCommandList* commandList;
	};

	struct Fence
	{
		void Create();
		void Release();

		ID3D12Fence* fence;
		HANDLE fenceEvent;
	};*/

	struct Upload
	{
		ID3D12CommandQueue* commandQueue;
		ID3D12CommandAllocator* commandAllocator;
		ID3D12GraphicsCommandList* commandList;
		HBuffer buffer;
		uint32_t bufferByteCount;
		ID3D12Fence* fence;
		UINT64 fenceValue;
		HANDLE fenceEvent;
	};

	struct DurationQuery
	{
		char name[64];
		Handle handle;
		uint32_t queryIndex; // indexes the heap
		QueryState::Id state;
	};

	struct FrameQueries
	{
		DurationQuery durationQueries[MaxDurationQueries];
		uint32_t durationQueryCount;
	};

	struct ResolvedDurationQuery
	{
		char name[64];
		uint32_t gpuMicroSeconds;
	};

	struct ResolvedQueries
	{
		ResolvedDurationQuery durationQueries[MaxDurationQueries];
		uint32_t durationQueryCount;
	};

	struct ShaderVisibleDescriptorHeap
	{
		void Init(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t* descriptors, uint32_t count, const char* name);
		void Release();
		uint32_t AllocateDescriptor();
		void FreeDescriptor(uint32_t index);

		ID3D12DescriptorHeap* mHeap;
		uint32_t* mDescriptors;
		uint32_t mDescriptorFreeList;
		uint32_t mCount;
	};

	struct RHIPrivate
	{
		ID3D12Debug* debug; // can be NULL
		ID3D12InfoQueue* infoQueue; // can be NULL
		IDXGIInfoQueue* dxgiInfoQueue; // can be NULL
	#if defined(_DEBUG)
		IDXGIFactory2* factory;
	#else
		IDXGIFactory1* factory;
	#endif
		IDXGIAdapter1* adapter;
		ID3D12Device* device;
		D3D12MA::Allocator* allocator;
		ID3D12CommandQueue* commandQueue;
		IDXGISwapChain3* swapChain;
		ID3D12DescriptorHeap* rtvHeap;
		ID3D12DescriptorHeap* imguiHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		UINT rtvIncSize;
		ID3D12Resource* renderTargets[FrameCount];
		ID3D12CommandAllocator* commandAllocators[FrameCount];
		ID3D12GraphicsCommandList* commandList;
		UINT frameIndex;
		HANDLE fenceEvent;
		ID3D12Fence* fence;
		UINT64 fenceValues[FrameCount];
		ID3D12QueryHeap* timeStampHeap;
		HBuffer timeStampBuffer;
		const UINT64* mappedTimeStamps;
		uint32_t durationQueryIndex;

#define POOL(Type, Size) StaticPool<Type, H##Type, ResourceType::Type, Size>
		POOL(Fence, 64) fences;
		POOL(Buffer, 64) buffers;
		POOL(Texture, MAX_DRAWIMAGES * 2) textures;
		POOL(RootSignature, 64) rootSignatures;
		POOL(Pipeline, 64) pipelines;
#undef POOL

		Upload upload;
		StaticUnorderedArray<HTexture, MAX_DRAWIMAGES> texturesToTransition;
		FrameQueries frameQueries[FrameCount];
		ResolvedQueries resolvedQueries;

		uint32_t descTex2D[RHI_MAX_TEXTURES_2D];
		uint32_t descSamplers[RHI_MAX_SAMPLERS];
		ShaderVisibleDescriptorHeap descHeapTex2D;
		ShaderVisibleDescriptorHeap descHeapSamplers;
	};

	static RHIPrivate rhi;

#define COM_RELEASE(p)       do { if(p) { p->Release(); p = NULL; } } while((void)0,0)
#define COM_RELEASE_ARRAY(a) do { for(int i = 0; i < ARRAY_LEN(a); ++i) { COM_RELEASE(a[i]); } } while((void)0,0)

#define D3D(Exp) Check((Exp), #Exp)

#if defined(near)
#	undef near
#endif

#if defined(far)
#	undef far
#endif

#if !defined(D3DDDIERR_DEVICEREMOVED)
#	define D3DDDIERR_DEVICEREMOVED ((HRESULT)0x88760870L)
#endif

	static const char* GetSystemErrorString(HRESULT hr)
	{
		// FormatMessage might not always give us the string we want but that's ok,
		// we always print the original error code anyhow
		static char systemErrorStr[1024];
		const DWORD written = FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			systemErrorStr, sizeof(systemErrorStr) - 1, NULL);
		if(written == 0)
		{
			// we have nothing valid
			Q_strncpyz(systemErrorStr, "???", sizeof(systemErrorStr));
		}
		else
		{
			// remove the trailing whitespace
			char* s = systemErrorStr + strlen(systemErrorStr) - 1;
			while(s >= systemErrorStr)
			{
				if(*s == '\r' || *s == '\n' || *s == '\t' || *s == ' ')
				{
					*s-- = '\0';
				}
				else
				{
					break;
				}
			}
		}

		return systemErrorStr;
	}

	static bool Check(HRESULT hr, const char* function)
	{
		if(SUCCEEDED(hr))
		{
			return true;
		}

		if(1) // @TODO: fatal error mode always on for now
		{
			ri.Error(ERR_FATAL, "'%s' failed with code 0x%08X (%s)\n", function, (unsigned int)hr, GetSystemErrorString(hr));
		}
		return false;
	}

	static void SetDebugName(ID3D12DeviceChild* resource, const char* resourceName)
	{
		if(resourceName == NULL)
		{
			return;
		}

		// ID3D12Object::SetName is a Unicode wrapper for
		// ID3D12Object::SetPrivateData with WKPDID_D3DDebugObjectNameW
		resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(resourceName), resourceName);
	}

	void ShaderVisibleDescriptorHeap::Init(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t* descriptors, uint32_t count, const char* name)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { 0 };
		heapDesc.Type = type;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NumDescriptors = count;
		heapDesc.NodeMask = 0;
		D3D(rhi.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap)));
		SetDebugName(mHeap, name);

		for(uint32_t d = 0; d < count; ++d)
		{
			descriptors[d] = d + 1;
		}
		mDescriptorFreeList = 0;
		mDescriptors = descriptors;
		mCount = count;
	}

	void ShaderVisibleDescriptorHeap::Release()
	{
		COM_RELEASE(mHeap);
	}

	uint32_t ShaderVisibleDescriptorHeap::AllocateDescriptor()
	{
		Q_assert(mDescriptorFreeList != UINT32_MAX);
		// @TODO: fatal error in release

		const uint32_t index = mDescriptorFreeList;
		mDescriptorFreeList = mDescriptors[index];
		mDescriptors[index] = UINT32_MAX;

		return index;
	}

	void ShaderVisibleDescriptorHeap::FreeDescriptor(uint32_t index)
	{
		Q_assert(index < mCount);
		// @TODO: fatal error in release

		const uint32_t oldList = mDescriptorFreeList;
		mDescriptorFreeList = index;
		mDescriptors[index] = oldList;
	}

	static const char* GetDeviceRemovedReasonString(HRESULT reason)
	{
		switch(reason)
		{
			case DXGI_ERROR_DEVICE_HUNG: return "device hung";
			case DXGI_ERROR_DEVICE_REMOVED: return "device removed";
			case DXGI_ERROR_DEVICE_RESET: return "device reset";
			case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "internal driver error";
			case DXGI_ERROR_INVALID_CALL: return "invalid call";
			case S_OK: return "no error";
			default: return va("unknown error code 0x%08X", (unsigned int)reason);
		}
	}

	static DXGI_GPU_PREFERENCE GetGPUPreference(gpuPreference_t preference)
	{
		switch(preference)
		{
			case GPU_PREFERENCE_HIGH_PERFORMANCE: return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
			case GPU_PREFERENCE_LOW_POWER: return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
			default: return DXGI_GPU_PREFERENCE_UNSPECIFIED;
		}
	}

	static bool IsSuitableAdapter(IDXGIAdapter1* adapter)
	{
		DXGI_ADAPTER_DESC1 desc;
		if(FAILED(adapter->GetDesc1(&desc)))
		{
			return false;
		}

		if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			return false;
		}

		if(FAILED(D3D12CreateDevice(adapter, FeatureLevel, __uuidof(ID3D12Device), NULL)))
		{
			return false;
		}

		return true;
	}

	static IDXGIAdapter1* FindMostSuitableAdapter(IDXGIFactory1* factory, gpuPreference_t enginePreference)
	{
		IDXGIAdapter1* adapter = NULL;
		IDXGIFactory6* factory6 = NULL;
		if(SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6))))
		{
			const DXGI_GPU_PREFERENCE dxgiPreference = GetGPUPreference(enginePreference);

			UINT i = 0;
			while(SUCCEEDED(factory6->EnumAdapterByGpuPreference(i++, dxgiPreference, IID_PPV_ARGS(&adapter))))
			{
				if(IsSuitableAdapter(adapter))
				{
					COM_RELEASE(factory6);
					return adapter;
				}
				COM_RELEASE(adapter);
			}
		}
		COM_RELEASE(factory6);

		UINT i = 0;
		while(SUCCEEDED(rhi.factory->EnumAdapters1(i++, &adapter)))
		{
			if(IsSuitableAdapter(adapter))
			{
				return adapter;
			}
			COM_RELEASE(adapter);
		}

		ri.Error(ERR_FATAL, "No suitable DXGI adapter was found!\n");
		return NULL;
	}

	static void MoveToNextFrame()
	{
		const UINT64 currentFenceValue = rhi.fenceValues[rhi.frameIndex];
		D3D(rhi.commandQueue->Signal(rhi.fence, currentFenceValue));

		rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();

		// wait indefinitely to start rendering if needed
		if(rhi.fence->GetCompletedValue() < rhi.fenceValues[rhi.frameIndex])
		{
			D3D(rhi.fence->SetEventOnCompletion(rhi.fenceValues[rhi.frameIndex], rhi.fenceEvent));
			WaitForSingleObjectEx(rhi.fenceEvent, INFINITE, FALSE);
		}

		rhi.fenceValues[rhi.frameIndex] = currentFenceValue + 1;
	}

	static void WaitUntilDeviceIsIdle()
	{
		D3D(rhi.commandQueue->Signal(rhi.fence, rhi.fenceValues[rhi.frameIndex]));

		D3D(rhi.fence->SetEventOnCompletion(rhi.fenceValues[rhi.frameIndex], rhi.fenceEvent));
		WaitForSingleObjectEx(rhi.fenceEvent, INFINITE, FALSE);

		rhi.fenceValues[rhi.frameIndex]++;
	}

	static void Present()
	{
		// DXGI_PRESENT_ALLOW_TEARING
		const HRESULT hr = rhi.swapChain->Present(abs(r_swapInterval->integer), 0);

		enum PresentError
		{
			PE_NONE,
			PE_DEVICE_REMOVED,
			PE_DEVICE_RESET
		};
		PresentError presentError = PE_NONE;
		HRESULT deviceRemovedReason = S_OK;
		if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == D3DDDIERR_DEVICEREMOVED)
		{
			deviceRemovedReason = rhi.device->GetDeviceRemovedReason();
			if(deviceRemovedReason == DXGI_ERROR_DEVICE_RESET)
			{
				presentError = PE_DEVICE_RESET;
			}
			else
			{
				presentError = PE_DEVICE_REMOVED;
			}
		}
		else if(hr == DXGI_ERROR_DEVICE_RESET)
		{
			presentError = PE_DEVICE_RESET;
		}

		if(presentError == PE_DEVICE_REMOVED)
		{
			ri.Error(ERR_FATAL, "Direct3D device was removed! Reason: %s", GetDeviceRemovedReasonString(deviceRemovedReason));
		}
		else if(presentError == PE_DEVICE_RESET)
		{
			ri.Printf(PRINT_ERROR, "Direct3D device was reset! Restarting the video system...");
			Cmd_ExecuteString("vid_restart;");
		}
	}

	static bool CanWriteCommands()
	{
		// @TODO:
		//return rhi.commandList != NULL && rhi.commandList->???
		return rhi.commandList != NULL;
	}

	static DXGI_FORMAT GetD3DIndexFormat(IndexType::Id type)
	{
		return type == IndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	}

	static D3D12_SHADER_VISIBILITY GetD3DVisibility(ShaderType::Id shaderType)
	{
		switch(shaderType)
		{
			case ShaderType::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
			case ShaderType::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
			case ShaderType::Compute: return (D3D12_SHADER_VISIBILITY)0; // @TODO: assert here too?
			default: Q_assert(!"Unsupported shader type"); return (D3D12_SHADER_VISIBILITY)0;
		}
	}

	static const char* GetD3DSemanticName(ShaderSemantic::Id semantic)
	{
		switch(semantic)
		{
			case ShaderSemantic::Position: return "POSITION";
			case ShaderSemantic::Normal: return "NORMAL";
			case ShaderSemantic::TexCoord: return "TEXCOORD";
			case ShaderSemantic::Color: return "COLOR";
			default: Q_assert(!"Unsupported shader semantic"); return "";
		}
	}

	static DXGI_FORMAT GetD3DFormat(DataType::Id dataType, uint32_t vectorLength)
	{
		if(vectorLength < 1 || vectorLength > 4)
		{
			Q_assert(!"Invalid vector length");
			return DXGI_FORMAT_UNKNOWN;
		}

		switch(dataType)
		{
			case DataType::Float32:
				switch(vectorLength)
				{
					case 1: return DXGI_FORMAT_R32_FLOAT;
					case 2: return DXGI_FORMAT_R32G32_FLOAT;
					case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
					case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
				}
			case DataType::UInt32:
				switch(vectorLength)
				{
					case 1: return DXGI_FORMAT_R32_UINT;
					case 2: return DXGI_FORMAT_R32G32_UINT;
					case 3: return DXGI_FORMAT_R32G32B32_UINT;
					case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
				}
			case DataType::UNorm8:
				switch(vectorLength)
				{
					case 1: return DXGI_FORMAT_R8_UNORM;
					case 2: return DXGI_FORMAT_R8G8_UNORM;
					case 3: Q_assert(!"Unsupported format"); return DXGI_FORMAT_UNKNOWN;
					case 4: return DXGI_FORMAT_R8G8B8A8_UNORM;
				}
			default: Q_assert(!"Unsupported data type"); return DXGI_FORMAT_UNKNOWN;
		}
	}

	static D3D12_COMPARISON_FUNC GetD3DComparisonFunction(ComparisonFunction::Id function)
	{
		switch(function)
		{
			case ComparisonFunction::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
			case ComparisonFunction::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
			case ComparisonFunction::Greater: return D3D12_COMPARISON_FUNC_GREATER;
			case ComparisonFunction::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			case ComparisonFunction::Less: return D3D12_COMPARISON_FUNC_LESS;
			case ComparisonFunction::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
			case ComparisonFunction::Never: return D3D12_COMPARISON_FUNC_NEVER;
			case ComparisonFunction::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
			default: Q_assert(!"Unsupported comparison function"); return D3D12_COMPARISON_FUNC_ALWAYS;
		}
	}

	static DXGI_FORMAT GetD3DFormat(TextureFormat::Id format)
	{
		switch(format)
		{
			case TextureFormat::RGBA32_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::DepthStencil32_UNorm24_UInt8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
			default: Q_assert(!"Unsupported texture format"); return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	static D3D12_CULL_MODE GetD3DCullMode(CullMode::Id cullMode)
	{
		switch(cullMode)
		{
			case CullMode::None: return D3D12_CULL_MODE_NONE;
			case CullMode::Front: return D3D12_CULL_MODE_FRONT;
			case CullMode::Back: return D3D12_CULL_MODE_BACK;
			default: Q_assert(!"Unsupported cull mode"); return D3D12_CULL_MODE_NONE;
		}
	}

	static D3D12_BLEND GetD3DSourceBlend(uint32_t stateBits)
	{
		switch(stateBits & GLS_SRCBLEND_BITS)
		{
			case GLS_SRCBLEND_ZERO: return D3D12_BLEND_ZERO;
			case GLS_SRCBLEND_ONE: return D3D12_BLEND_ONE;
			case GLS_SRCBLEND_DST_COLOR: return D3D12_BLEND_DEST_COLOR;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
			case GLS_SRCBLEND_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
			case GLS_SRCBLEND_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
			case GLS_SRCBLEND_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
			default: Q_assert(!"Unsupported source blend mode"); return D3D12_BLEND_ONE;
		}
	}

	static D3D12_BLEND GetD3DDestBlend(uint32_t stateBits)
	{
		switch(stateBits & GLS_DSTBLEND_BITS)
		{
			case GLS_DSTBLEND_ZERO: return D3D12_BLEND_ZERO;
			case GLS_DSTBLEND_ONE: return D3D12_BLEND_ONE;
			case GLS_DSTBLEND_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
			case GLS_DSTBLEND_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
			case GLS_DSTBLEND_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
			default: Q_assert(!"Unsupported dest blend mode"); return D3D12_BLEND_ONE;
		}
	}

	static D3D12_BLEND GetAlphaBlendFromColorBlend(D3D12_BLEND colorBlend)
	{
		switch(colorBlend)
		{
			case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
			case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
			case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
			case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
			default: return colorBlend;
		}
	}

	static UINT64 GetUploadBufferSize(ID3D12Resource* resource, UINT firstSubresource, UINT subresourceCount)
	{
		UINT64 requiredSize = 0;
		const D3D12_RESOURCE_DESC desc = resource->GetDesc();
		rhi.device->GetCopyableFootprints(&desc, firstSubresource, subresourceCount, 0, NULL, NULL, NULL, &requiredSize);

		return requiredSize;
	}

	static UINT IsPowerOfTwo(UINT x)
	{
		return x > 0 && (x & (x - 1)) == 0;
	}

	static UINT AlignUp(UINT value, UINT alignment)
	{
		Q_assert(IsPowerOfTwo(alignment));

		const UINT mask = alignment - 1;

		return (value + mask) & (~mask);
	}

	static void ResolveDurationQueries()
	{
		UINT64 gpuFrequency;
		D3D(rhi.commandQueue->GetTimestampFrequency(&gpuFrequency));
		const double frequency = (double)gpuFrequency;

		const uint32_t frameIndex = rhi.frameIndex ^ 1;
		FrameQueries& fq = rhi.frameQueries[frameIndex];
		ResolvedQueries& rq = rhi.resolvedQueries;
		const UINT64* const timeStamps = rhi.mappedTimeStamps + (frameIndex * MaxDurationQueries * 2);

		for(uint32_t q = 0; q < fq.durationQueryCount; ++q)
		{
			DurationQuery& dq = fq.durationQueries[q];
			ResolvedDurationQuery& rdq = rq.durationQueries[q];

			const UINT64 begin = timeStamps[dq.queryIndex * 2 + 0];
			const UINT64 end = timeStamps[dq.queryIndex * 2 + 1];
			if(end > begin)
			{
				const UINT64 delta = end - begin;
				rdq.gpuMicroSeconds = (uint32_t)((delta / frequency) * 1000000.0);
			}
			else
			{
				rdq.gpuMicroSeconds = 0;
			}
			Q_strncpyz(rdq.name, dq.name, sizeof(rdq.name));

			dq.state = QueryState::Free;
		}

		rq.durationQueryCount = fq.durationQueryCount;
		fq.durationQueryCount = 0;
	}

	static void DrawPerfStats()
	{
		if(ImGui::Begin("Performance"))
		{
			if(ImGui::BeginTable("GPU timings", 2))
			{
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("Micro-seconds");
				ImGui::TableHeadersRow();

				const ResolvedQueries& rq = rhi.resolvedQueries;
				for(uint32_t q = 0; q < rq.durationQueryCount; ++q)
				{
					const ResolvedDurationQuery& rdq = rq.durationQueries[q];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text(rdq.name);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%d", (int)rdq.gpuMicroSeconds);
				}

				ImGui::EndTable();
			}
		}
		ImGui::End();
	}

	void Init()
	{
		Sys_V_Init();

		if(rhi.device != NULL)
		{
			DXGI_SWAP_CHAIN_DESC desc;
			D3D(rhi.swapChain->GetDesc(&desc));

			if(glConfig.vidWidth != desc.BufferDesc.Width ||
				glConfig.vidHeight != desc.BufferDesc.Height)
			{
				WaitUntilDeviceIsIdle();

				for(uint32_t f = 0; f < FrameCount; ++f)
				{
					COM_RELEASE(rhi.renderTargets[f]);
				}
				
				D3D(rhi.swapChain->ResizeBuffers(desc.BufferCount, glConfig.vidWidth, glConfig.vidHeight, desc.BufferDesc.Format, desc.Flags));				

				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleIt = rhi.rtvHandle;
				for(uint32_t f = 0; f < FrameCount; ++f)
				{
					D3D(rhi.swapChain->GetBuffer(f, IID_PPV_ARGS(&rhi.renderTargets[f])));
					rhi.device->CreateRenderTargetView(rhi.renderTargets[f], NULL, rtvHandleIt);
					SetDebugName(rhi.renderTargets[f], va("Swap Chain RTV #%d", f + 1));
					rtvHandleIt.ptr += rhi.rtvIncSize;
				}

				rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();

				for(uint32_t f = 0; f < FrameCount; ++f)
				{
					rhi.fenceValues[f] = 0;
				}
				rhi.fenceValues[rhi.frameIndex]++;
			}

			return;
		}

		// @NOTE: we can't use memset because of the StaticPool members
		new (&rhi) RHIPrivate();

#if defined(_DEBUG)
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&rhi.debug))))
		{
			// calling after device creation will remove the device
			// if you hit this error:
			// D3D Error 887e0003: (13368@153399640) at 00007FFC84ECF985 - D3D12 SDKLayers dll does not match the D3D12SDKVersion of D3D12 Core dll.
			// make sure your D3D12SDKVersion and D3D12SDKPath are valid!
			rhi.debug->EnableDebugLayer();
		}

		UINT dxgiFactoryFlags = 0;
		if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&rhi.dxgiInfoQueue))))
		{
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			rhi.dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
			rhi.dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		}
#endif

#if defined(_DEBUG)
		D3D(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&rhi.factory)));
#else
		D3D(CreateDXGIFactory1(IID_PPV_ARGS(&rhi.factory)));
#endif

		rhi.adapter = FindMostSuitableAdapter(rhi.factory, GPU_PREFERENCE_HIGH_PERFORMANCE);

		D3D(D3D12CreateDevice(rhi.adapter, FeatureLevel, IID_PPV_ARGS(&rhi.device)));

		{
			D3D12MA::ALLOCATOR_DESC desc = {};
			desc.pDevice = rhi.device;
			desc.pAdapter = rhi.adapter;
			desc.Flags = D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED;
			D3D(D3D12MA::CreateAllocator(&desc, &rhi.allocator));
		}

#if defined(_DEBUG)
		if(rhi.debug)
		{
			rhi.device->QueryInterface(IID_PPV_ARGS(&rhi.infoQueue));
			if(rhi.infoQueue)
			{
				rhi.infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				rhi.infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

				D3D12_MESSAGE_ID filteredMessages[] =
				{
					D3D12_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS
				};
				D3D12_INFO_QUEUE_FILTER filter = { 0 };
				filter.DenyList.NumIDs = ARRAY_LEN(filteredMessages);
				filter.DenyList.pIDList = filteredMessages;
				rhi.infoQueue->AddStorageFilterEntries(&filter);
			}
		}
#endif

		rhi.descHeapTex2D.Init(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, rhi.descTex2D, ARRAY_LEN(rhi.descTex2D), "Texture2D heap");
		rhi.descHeapSamplers.Init(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, rhi.descSamplers, ARRAY_LEN(rhi.descSamplers), "Sampler heap");

		{
			D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { 0 };
			commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			commandQueueDesc.NodeMask = 0;
			D3D(rhi.device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&rhi.commandQueue)));
			SetDebugName(rhi.commandQueue, "Main Command Queue");
		}

		{
			IDXGISwapChain* dxgiSwapChain;
			DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
			swapChainDesc.BufferCount = FrameCount;
			swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.BufferDesc.Width = glInfo.winWidth;
			swapChainDesc.BufferDesc.Height = glInfo.winHeight;
			swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
			swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
			swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.Flags = 0; // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
			swapChainDesc.OutputWindow = GetActiveWindow();
			swapChainDesc.SampleDesc.Count = 1;
			swapChainDesc.SampleDesc.Quality = 0;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.Windowed = TRUE;
			D3D(rhi.factory->CreateSwapChain(rhi.commandQueue, &swapChainDesc, &dxgiSwapChain));

			D3D(dxgiSwapChain->QueryInterface(IID_PPV_ARGS(&rhi.swapChain)));
			rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();
			COM_RELEASE(dxgiSwapChain);
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { 0 };
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			rtvHeapDesc.NumDescriptors = FrameCount;
			rtvHeapDesc.NodeMask = 0;
			D3D(rhi.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rhi.rtvHeap)));
			SetDebugName(rhi.rtvHeap, "Swap Chain RTV Descriptor Heap");

			rhi.rtvIncSize = rhi.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			rhi.rtvHandle = rhi.rtvHeap->GetCPUDescriptorHandleForHeapStart();
		}

		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleIt = rhi.rtvHandle;
			for(UINT f = 0; f < FrameCount; ++f)
			{
				D3D(rhi.swapChain->GetBuffer(f, IID_PPV_ARGS(&rhi.renderTargets[f])));
				rhi.device->CreateRenderTargetView(rhi.renderTargets[f], NULL, rtvHandleIt);
				SetDebugName(rhi.renderTargets[f], va("Swap Chain RTV #%d", f + 1));
				rtvHandleIt.ptr += rhi.rtvIncSize;

				D3D(rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rhi.commandAllocators[f])));
				SetDebugName(rhi.commandAllocators[f], va("Command Allocator #%d", f + 1));
			}
		}

		// get command list ready to use during init
		D3D(rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rhi.commandAllocators[rhi.frameIndex], NULL, IID_PPV_ARGS(&rhi.commandList)));
		SetDebugName(rhi.commandList, "Command List");

		D3D(rhi.device->CreateFence(rhi.fenceValues[rhi.frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&rhi.fence)));
		SetDebugName(rhi.fence, "Command Queue Fence");
		rhi.fenceValues[rhi.frameIndex]++;

		rhi.fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(rhi.fenceEvent == NULL)
		{
			Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { 0 };
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapDesc.NumDescriptors = 64;
			heapDesc.NodeMask = 0;
			D3D(rhi.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rhi.imguiHeap)));
			SetDebugName(rhi.imguiHeap, "Dear ImGUI Descriptor Heap");
		}

		{
			// @TODO: uint32_t CreateSampler(const SamplerDesc& desc);
			D3D12_SAMPLER_DESC desc = { 0 };
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
			desc.MaxAnisotropy = 1;
			desc.MaxLOD = D3D12_FLOAT32_MAX;
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			rhi.device->CreateSampler(&desc, rhi.descHeapSamplers.mHeap->GetCPUDescriptorHandleForHeapStart());
		}

		{
			BufferDesc bufferDesc = { 0 };
			bufferDesc.name = "upload buffer";
			bufferDesc.byteCount = 64 << 20;
			bufferDesc.memoryUsage = MemoryUsage::Upload;
			bufferDesc.initialState = ResourceState::CopyDestinationBit;
			bufferDesc.committedResource = true;
			rhi.upload.buffer = CreateBuffer(bufferDesc);
			rhi.upload.bufferByteCount = bufferDesc.byteCount;
		}

		{
			D3D12_COMMAND_QUEUE_DESC desc = { 0 };
			desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			desc.NodeMask = 0;
			D3D(rhi.device->CreateCommandQueue(&desc, IID_PPV_ARGS(&rhi.upload.commandQueue)));
			SetDebugName(rhi.upload.commandQueue, "copy command queue");
		}

		D3D(rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&rhi.upload.commandAllocator)));
		SetDebugName(rhi.upload.commandAllocator, "copy command allocator");

		D3D(rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, rhi.upload.commandAllocator, NULL, IID_PPV_ARGS(&rhi.upload.commandList)));
		SetDebugName(rhi.upload.commandList, "copy command list");
		D3D(rhi.upload.commandList->Close());

		D3D(rhi.device->CreateFence(rhi.upload.fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&rhi.upload.fence)));
		SetDebugName(rhi.upload.fence, "Copy Queue Fence");

		rhi.upload.fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(rhi.upload.fenceEvent == NULL)
		{
			Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
		}

		{
			rhi.durationQueryIndex = 0;
			D3D12_QUERY_HEAP_DESC desc = { 0 };
			desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			desc.Count = MaxDurationQueries * 2;
			desc.NodeMask = 0;
			D3D(rhi.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&rhi.timeStampHeap)));
		}

		{
			BufferDesc desc = { 0 };
			desc.name = "TimeStamp Readback Buffer";
			desc.byteCount = MaxDurationQueries * 2 * FrameCount * sizeof(UINT64);
			desc.initialState = ResourceState::CopySourceBit;
			desc.memoryUsage = MemoryUsage::Readback;
			desc.committedResource = true;
			rhi.timeStampBuffer = CreateBuffer(desc);
			rhi.mappedTimeStamps = (UINT64*)MapBuffer(rhi.timeStampBuffer);
		}

		// queue some actual work...

		D3D(rhi.commandList->Close());

		WaitUntilDeviceIsIdle();

		glInfo.maxTextureSize = 2048;
		glInfo.maxAnisotropy = 16;
		glInfo.depthFadeSupport = qfalse;

		if(!ImGui_ImplDX12_Init(rhi.device, FrameCount, DXGI_FORMAT_R8G8B8A8_UNORM, rhi.imguiHeap,
			rhi.imguiHeap->GetCPUDescriptorHandleForHeapStart(),
			rhi.imguiHeap->GetGPUDescriptorHandleForHeapStart()))
		{
			ri.Error(ERR_FATAL, "Failed to initialize graphics objects for Dear ImGUI\n");
		}
	}

	void ShutDown(qbool destroyWindow)
	{
#define DESTROY_POOL(PoolName, FuncName) \
		for(int i = 0; rhi.PoolName.FindNext(&handle, &i);) \
			FuncName(MAKE_HANDLE(handle)); \
		rhi.PoolName.Clear()

		Handle handle;

		if(!destroyWindow)
		{
			WaitUntilDeviceIsIdle();

			// @TODO: the GRP will nuke all 2D textures it has to itself
			//DESTROY_POOL(textures, DestroyTexture);

			return;
		}

		ImGui_ImplDX12_Shutdown();

		WaitUntilDeviceIsIdle();

		rhi.descHeapTex2D.Release();
		rhi.descHeapSamplers.Release();

		//DESTROY_POOL(fences, DestroyFence);
		DESTROY_POOL(buffers, DestroyBuffer);
		DESTROY_POOL(textures, DestroyTexture);
		DESTROY_POOL(rootSignatures, DestroyRootSignature);
		DESTROY_POOL(pipelines, DestroyPipeline);

		CloseHandle(rhi.upload.fenceEvent);
		COM_RELEASE(rhi.upload.fence);
		COM_RELEASE(rhi.upload.commandList);
		COM_RELEASE(rhi.upload.commandAllocator);
		COM_RELEASE(rhi.upload.commandQueue);

		COM_RELEASE(rhi.timeStampHeap);
		CloseHandle(rhi.fenceEvent);
		COM_RELEASE(rhi.fence);
		COM_RELEASE(rhi.commandList);
		COM_RELEASE_ARRAY(rhi.commandAllocators);
		COM_RELEASE_ARRAY(rhi.renderTargets);
		COM_RELEASE(rhi.imguiHeap);
		COM_RELEASE(rhi.rtvHeap);
		COM_RELEASE(rhi.swapChain);
		COM_RELEASE(rhi.commandQueue);
		COM_RELEASE(rhi.infoQueue);
		COM_RELEASE(rhi.allocator);
		COM_RELEASE(rhi.device);
		COM_RELEASE(rhi.adapter);
		COM_RELEASE(rhi.factory);
		COM_RELEASE(rhi.dxgiInfoQueue);
		COM_RELEASE(rhi.debug);
		
#if defined(_DEBUG)
		IDXGIDebug1* debug = NULL;
		if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
		{
			// DXGI_DEBUG_RLO_ALL is DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL
			OutputDebugStringA("**** >>>> CNQ3: calling ReportLiveObjects\n");
			const HRESULT hr = debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			OutputDebugStringA(va("**** >>>> CNQ3: ReportLiveObjects returned 0x%08X (%s)\n", (unsigned int)hr, GetSystemErrorString(hr)));
			debug->Release();
		}
#endif

#undef DESTROY_POOL
	}

	static HDurationQuery frameDuration;

	void BeginFrame()
	{
		// reclaim used memory
		D3D(rhi.commandAllocators[rhi.frameIndex]->Reset());

		// start recording
		D3D(rhi.commandList->Reset(rhi.commandAllocators[rhi.frameIndex], NULL));

		frameDuration = CmdBeginDurationQuery("Whole frame");

		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = rhi.renderTargets[rhi.frameIndex];
		barrier.Transition.Subresource = 0; // D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rhi.commandList->ResourceBarrier(1, &barrier);

		ID3D12DescriptorHeap* heaps[2] = { rhi.descHeapTex2D.mHeap, rhi.descHeapSamplers.mHeap };
		rhi.commandList->SetDescriptorHeaps(ARRAY_LEN(heaps), heaps);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { 0 };
		rtvHandle.ptr = rhi.rtvHandle.ptr + rhi.frameIndex * rhi.rtvIncSize;
		rhi.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
		const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		rhi.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);

		// @TODO: keep this? run through compute queue first?
		for(uint32_t t = 0; t < rhi.texturesToTransition.count; ++t)
		{
			// @TODO: transition all mips anyway?
			Texture& texture = rhi.textures.Get(rhi.texturesToTransition[t]);
			D3D12_RESOURCE_BARRIER b = { 0 };
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			b.Transition.pResource = texture.texture;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			b.Transition.Subresource = 0;
			rhi.commandList->ResourceBarrier(1, &b);
			texture.subResources[0].state = ResourceState::PixelShaderAccessBit;
		}
		rhi.texturesToTransition.Clear();
	}

	void EndFrame()
	{
		if(r_debugUI->integer)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize.x = glConfig.vidWidth;
			io.DisplaySize.y = glConfig.vidHeight;
			ImGui_ImplDX12_NewFrame();
			ImGui::NewFrame();
			DrawPerfStats();
			//ImGui::ShowDemoWindow();
			ImGui::EndFrame();
			ImGui::Render();
			rhi.commandList->SetDescriptorHeaps(1, &rhi.imguiHeap);
			// the following call will set SetGraphicsRootDescriptorTable itself
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), rhi.commandList);
		}

		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = rhi.renderTargets[rhi.frameIndex];
		barrier.Transition.Subresource = 0; // D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		rhi.commandList->ResourceBarrier(1, &barrier);

		CmdEndDurationQuery(frameDuration);

		// stop recording
		D3D(rhi.commandList->Close());

		ID3D12CommandList* commandListArray[] = { rhi.commandList };
		rhi.commandQueue->ExecuteCommandLists(ARRAY_LEN(commandListArray), commandListArray);

		Present();

		MoveToNextFrame();

		ResolveDurationQueries();
	}

	uint32_t GetFrameIndex()
	{
		return rhi.frameIndex;
	}

	HFence CreateFence()
	{
		// @TODO:
		Fence fence = { 0 };
		//fence.fence = ;
		//fence.fenceEvent = ;
		//fence.fenceValue = ;

		return rhi.fences.Add(fence);
	}

	void DestroyFence(HFence fence)
	{
		// @TODO:
	}

	void WaitForAllFences(uint32_t fenceCount, const HFence* fences)
	{
		// @TODO:
	}

	HBuffer CreateBuffer(const BufferDesc& rhiDesc)
	{
		// alignment must be 64KB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
		// https://msdn.microsoft.com/en-us/library/windows/desktop/dn903813(v=vs.85).aspx
		D3D12_RESOURCE_DESC desc = { 0 };
		desc.Alignment = 0; // D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
		desc.DepthOrArraySize = 1;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.Width = rhiDesc.byteCount;
		desc.Height = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		if(rhiDesc.initialState & ResourceState::UnorderedAccessBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
		D3D12MA::ALLOCATION_DESC allocDesc = { 0 };
		allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		if(rhiDesc.memoryUsage == MemoryUsage::CPU || rhiDesc.memoryUsage == MemoryUsage::Upload)
		{
			allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
			resourceState = D3D12_RESOURCE_STATE_GENERIC_READ; // @TODO: specialize this
		}
		else if(rhiDesc.memoryUsage == MemoryUsage::Readback)
		{
			allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
			//resourceState = D3D12_RESOURCE_STATE_COPY_SOURCE;
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
		allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_STRATEGY_MIN_MEMORY;
		if(rhiDesc.committedResource)
		{
			allocDesc.Flags = (D3D12MA::ALLOCATION_FLAGS)(allocDesc.Flags | D3D12MA::ALLOCATION_FLAG_COMMITTED);
		}

		D3D12MA::Allocation* allocation;
		ID3D12Resource* resource;
		D3D(rhi.allocator->CreateResource(&allocDesc, &desc, resourceState, NULL, &allocation, IID_PPV_ARGS(&resource)));
		SetDebugName(resource, rhiDesc.name);

		Buffer buffer = { 0 };
		buffer.desc = rhiDesc;
		buffer.allocation = allocation;
		buffer.buffer = resource;
		buffer.gpuAddress = resource->GetGPUVirtualAddress();

		return rhi.buffers.Add(buffer);
	}

	void DestroyBuffer(HBuffer handle)
	{
		Buffer& buffer = rhi.buffers.Get(handle);
		if(buffer.mapped)
		{
			UnmapBuffer(handle);
		}
		COM_RELEASE(buffer.buffer);
		COM_RELEASE(buffer.allocation);
		rhi.buffers.Remove(handle);
	}

	void* MapBuffer(HBuffer handle)
	{
		Buffer& buffer = rhi.buffers.Get(handle);
		if(buffer.mapped)
		{
			ri.Error(ERR_FATAL, "Attempted to map buffer '%s' that is already mapped!\n", buffer.desc.name);
			return NULL;
		}

		void* mappedPtr;
		D3D(buffer.buffer->Map(0, NULL, &mappedPtr));
		buffer.mapped = true;

		return mappedPtr;
	}

	void UnmapBuffer(HBuffer handle)
	{
		Buffer& buffer = rhi.buffers.Get(handle);
		if(!buffer.mapped)
		{
			ri.Error(ERR_FATAL, "Attempted to unmap buffer '%s' that isn't mapped!\n", buffer.desc.name);
			return;
		}

		buffer.buffer->Unmap(0, NULL);
		buffer.mapped = false;
	}

	HTexture CreateTexture(const TextureDesc& rhiDesc)
	{
		Q_assert(rhiDesc.width > 0);
		Q_assert(rhiDesc.height > 0);
		Q_assert(rhiDesc.sampleCount > 0);
		Q_assert(rhiDesc.mipCount > 0);
		Q_assert(rhiDesc.mipCount <= ARRAY_LEN(Texture::subResources));

		// alignment must be 64KB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
		// https://msdn.microsoft.com/en-us/library/windows/desktop/dn903813(v=vs.85).aspx
		D3D12_RESOURCE_DESC desc = { 0 };
		desc.Alignment = 0;
		desc.DepthOrArraySize = 1;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		desc.Format = GetD3DFormat(rhiDesc.format);
		desc.Width = rhiDesc.width;
		desc.Height = rhiDesc.height;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.MipLevels = rhiDesc.mipCount;
		desc.SampleDesc.Count = rhiDesc.sampleCount;
		desc.SampleDesc.Quality = 0;
		if(rhiDesc.initialState & ResourceState::UnorderedAccessBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		if(rhiDesc.initialState & ResourceState::RenderTargetBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		if(rhiDesc.initialState & ResourceState::DepthAccessBits)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}
		if((rhiDesc.initialState & ResourceState::ShaderAccessBits) == 0)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}

		// @TODO: leverage D3D12_HEAP_TYPE_CUSTOM for a UMA architecture?
		D3D12MA::ALLOCATION_DESC allocDesc = { 0 };
		allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_STRATEGY_MIN_MEMORY;
		if(rhiDesc.committedResource)
		{
			allocDesc.Flags = (D3D12MA::ALLOCATION_FLAGS)(allocDesc.Flags | D3D12MA::ALLOCATION_FLAG_COMMITTED);
		}

		// @TODO: initial state -> D3D12_RESOURCE_STATE
		// @TODO: clear value
		D3D12MA::Allocation* allocation;
		ID3D12Resource* resource;
		D3D(rhi.allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &allocation, IID_PPV_ARGS(&resource)));
		SetDebugName(resource, rhiDesc.name);

		D3D12_SHADER_RESOURCE_VIEW_DESC srv = { 0 };
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Format = desc.Format;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//srv.Texture2D.MipLevels = desc.MipLevels;
		srv.Texture2D.MipLevels = 1; // @TODO:
		srv.Texture2D.MostDetailedMip = 0;
		srv.Texture2D.PlaneSlice = 0;
		srv.Texture2D.ResourceMinLODClamp = 0.0f;

		const uint32_t srvIndex = rhi.descHeapTex2D.AllocateDescriptor();
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = rhi.descHeapTex2D.mHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += srvIndex * rhi.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		rhi.device->CreateShaderResourceView(resource, &srv, srvHandle);

		Texture texture = { 0 };
		texture.desc = rhiDesc;
		texture.allocation = allocation;
		texture.texture = resource;
		texture.srvIndex = srvIndex;
		for(int m = 0; m < rhiDesc.mipCount; ++m)
		{
			texture.subResources[m].state = rhiDesc.initialState;
		}

		return rhi.textures.Add(texture);
	}

	void UploadTextureMip0(HTexture handle, const TextureUploadDesc& desc)
	{
		// @TODO: support for sub-regions so that internal lightmaps get handled right

		Texture& texture = rhi.textures.Get(handle);

		// otherwise the pitch is computed wrong!
		Q_assert(texture.desc.format == TextureFormat::RGBA32_UNorm);

		const UINT64 uploadByteCount = GetUploadBufferSize(texture.texture, 0, 1);
		if(uploadByteCount > rhi.upload.bufferByteCount)
		{
			ri.Error(ERR_FATAL, "Upload request too large!\n");
		}

		D3D12_RESOURCE_DESC textureDesc = texture.texture->GetDesc();
		uint64_t textureMemorySize = 0;
		UINT numRows[16];
		UINT64 rowSizesInBytes[16];
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[16];
		const uint64_t numSubResources = texture.desc.mipCount;
		rhi.device->GetCopyableFootprints(&textureDesc, 0, (uint32_t)numSubResources, 0, layouts, numRows, rowSizesInBytes, &textureMemorySize);

		if(rhi.upload.fence->GetCompletedValue() < rhi.upload.fenceValue)
		{
			D3D(rhi.upload.fence->SetEventOnCompletion(rhi.upload.fenceValue, rhi.upload.fenceEvent));
			WaitForSingleObjectEx(rhi.upload.fenceEvent, INFINITE, FALSE);
		}
		rhi.upload.fenceValue++;

		{
			byte* const uploadMemory = (byte*)MapBuffer(rhi.upload.buffer);

			const byte* sourceSubResourceMemory = (const byte*)desc.data;
			const uint64_t subResourceIndex = 0;
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& subResourceLayout = layouts[subResourceIndex];
			const uint64_t subResourceHeight = numRows[subResourceIndex];
			const uint64_t subResourcePitch = AlignUp(subResourceLayout.Footprint.RowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
			const uint64_t sourceRowPitch = desc.width * 4; // @TODO: compute the pitch properly baded on the format...
			uint8_t* destinationSubResourceMemory = uploadMemory + subResourceLayout.Offset;

			for(uint64_t height = 0; height < subResourceHeight; height++)
			{
				memcpy(destinationSubResourceMemory, sourceSubResourceMemory, min(subResourcePitch, sourceRowPitch));
				destinationSubResourceMemory += subResourcePitch;
				sourceSubResourceMemory += sourceRowPitch;
			}

			UnmapBuffer(rhi.upload.buffer);
		}

		D3D(rhi.upload.commandAllocator->Reset());
		D3D(rhi.upload.commandList->Reset(rhi.upload.commandAllocator, NULL));

		Buffer& buffer = rhi.buffers.Get(rhi.upload.buffer);
		D3D12_TEXTURE_COPY_LOCATION dstLoc = { 0 };
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { 0 };
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.pResource = texture.texture;
		dstLoc.SubresourceIndex = 0;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.pResource = buffer.buffer;
		srcLoc.PlacedFootprint = layouts[0];
		rhi.upload.commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, NULL);

		// @TODO:
#if 0
		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = texture.texture;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = 0;
		rhi.upload.commandList->ResourceBarrier(1, &barrier);
		texture.subResources[0].state = ResourceState::PixelShaderAccessBit;
#endif

		ID3D12CommandList* commandLists[] = { rhi.upload.commandList };
		D3D(rhi.upload.commandList->Close());
		rhi.upload.commandQueue->ExecuteCommandLists(ARRAY_LEN(commandLists), commandLists);
		rhi.upload.commandQueue->Signal(rhi.upload.fence, rhi.upload.fenceValue);

		rhi.texturesToTransition.Add(handle);
	}

	void GenerateTextureMips(HTexture texture)
	{
		// @TODO:
	}

	uint32_t GetTextureSRV(HTexture texture)
	{
		return rhi.textures.Get(texture).srvIndex;
	}

	void DestroyTexture(HTexture handle)
	{
		Texture& texture = rhi.textures.Get(handle);
		COM_RELEASE(texture.texture);
		COM_RELEASE(texture.allocation);
		rhi.textures.Remove(handle);
	}

	HRootSignature CreateRootSignature(const RootSignatureDesc& rhiDesc)
	{
		RootSignature rhiSignature = { 0 };

		int parameterCount = 0;
		D3D12_ROOT_PARAMETER parameters[16];
		for(int s = 0; s < ShaderType::Count; ++s)
		{
			if(rhiDesc.constants[s].count > 0)
			{
				rhiSignature.constants[s].parameterIndex = parameterCount;

				D3D12_ROOT_PARAMETER& p = parameters[parameterCount];
				p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
				p.Constants.Num32BitValues = rhiDesc.constants[s].count;
				p.Constants.RegisterSpace = 0;
				p.Constants.ShaderRegister = 0;
				p.ShaderVisibility = GetD3DVisibility((ShaderType::Id)s);

				parameterCount++;
			}
		}
		Q_assert(parameterCount <= ShaderType::Count);
		const int firstTableIndex = parameterCount;

		D3D12_DESCRIPTOR_RANGE ranges[64] = {};
		int rangeCount = 0;

		{
			D3D12_DESCRIPTOR_RANGE& r = ranges[rangeCount++];
			r.BaseShaderRegister = 0;
			r.NumDescriptors = RHI_MAX_TEXTURES_2D;
			r.OffsetInDescriptorsFromTableStart = 0;
			r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			r.RegisterSpace = RHI_SPACE_TEXTURE2D;
			D3D12_ROOT_PARAMETER& p = parameters[parameterCount++];
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			p.DescriptorTable.NumDescriptorRanges = 1;
			p.DescriptorTable.pDescriptorRanges = &r;
			p.ShaderVisibility = (D3D12_SHADER_VISIBILITY)(D3D12_SHADER_VISIBILITY_VERTEX | D3D12_SHADER_VISIBILITY_PIXEL);
		}

		{
			D3D12_DESCRIPTOR_RANGE& r = ranges[rangeCount++];
			r.BaseShaderRegister = 0;
			r.NumDescriptors = RHI_MAX_SAMPLERS;
			r.OffsetInDescriptorsFromTableStart = 0;
			r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			r.RegisterSpace = RHI_SPACE_SAMPLERS;
			D3D12_ROOT_PARAMETER& p = parameters[parameterCount++];
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			p.DescriptorTable.NumDescriptorRanges = 1;
			p.DescriptorTable.pDescriptorRanges = &r;
			p.ShaderVisibility = (D3D12_SHADER_VISIBILITY)(D3D12_SHADER_VISIBILITY_VERTEX | D3D12_SHADER_VISIBILITY_PIXEL);
		}

		D3D12_ROOT_SIGNATURE_DESC desc = { 0 };
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
		if(rhiDesc.constants[ShaderType::Vertex].count == 0)
		{
			desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
		}
		if(rhiDesc.constants[ShaderType::Pixel].count == 0)
		{
			desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
		}
		if(rhiDesc.usingVertexBuffers)
		{
			desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		}
		desc.NumParameters = parameterCount;
		desc.pParameters = parameters;
		desc.NumStaticSamplers = 0;
		desc.pStaticSamplers = NULL;

		ID3DBlob* blob;
		ID3DBlob* errorBlob;
		if(FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob)))
		{
			ri.Error(ERR_FATAL, "Root signature creation failed!\n%s\n", (const char*)errorBlob->GetBufferPointer());
		}
		COM_RELEASE(errorBlob);

		ID3D12RootSignature* signature;
		D3D(rhi.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature)));
		COM_RELEASE(blob);

		rhiSignature.desc = rhiDesc;
		rhiSignature.signature = signature;
		rhiSignature.firstTableIndex = firstTableIndex;

		return rhi.rootSignatures.Add(rhiSignature);
	}

	void DestroyRootSignature(HRootSignature signature)
	{
		COM_RELEASE(rhi.rootSignatures.Get(signature).signature);
		rhi.rootSignatures.Remove(signature);
	}

	HPipeline CreateGraphicsPipeline(const GraphicsPipelineDesc& rhiDesc)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = { 0 };
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE; // none available so far
		desc.pRootSignature = rhi.rootSignatures.Get(rhiDesc.rootSignature).signature;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.SampleDesc.Count = 1;
		desc.SampleMask = UINT_MAX;

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[MaxVertexAttributeCount];
		for(int a = 0; a < rhiDesc.vertexLayout.attributeCount; ++a)
		{
			const VertexAttribute& va = rhiDesc.vertexLayout.attributes[a];
			D3D12_INPUT_ELEMENT_DESC& ied = inputElementDescs[a];
			ied.SemanticName = GetD3DSemanticName(va.semantic);
			ied.SemanticIndex = 0; // @TODO: need this if we want e.g. 2+ texture coordinates when interleaving
			ied.Format = GetD3DFormat(va.dataType, va.vectorLength);
			ied.InputSlot = va.vertexBufferIndex;
			ied.AlignedByteOffset = va.structByteOffset;
			ied.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			ied.InstanceDataStepRate = 0;
		}
		desc.InputLayout.NumElements = rhiDesc.vertexLayout.attributeCount;
		desc.InputLayout.pInputElementDescs = inputElementDescs;

		for(int t = 0; t < rhiDesc.renderTargetCount; ++t)
		{
			const GraphicsPipelineDesc::RenderTarget& rtIn = rhiDesc.renderTargets[t];
			D3D12_RENDER_TARGET_BLEND_DESC& rtOut = desc.BlendState.RenderTarget[t];
			rtOut.BlendEnable = TRUE;
			rtOut.BlendOp = D3D12_BLEND_OP_ADD;
			rtOut.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rtOut.LogicOpEnable = FALSE;
			rtOut.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // RGBA
			rtOut.SrcBlend = GetD3DSourceBlend(rtIn.q3BlendMode);
			rtOut.DestBlend = GetD3DDestBlend(rtIn.q3BlendMode);
			rtOut.SrcBlendAlpha = GetAlphaBlendFromColorBlend(rtOut.SrcBlend);
			rtOut.DestBlendAlpha = GetAlphaBlendFromColorBlend(rtOut.DestBlend);
			desc.RTVFormats[t] = GetD3DFormat(rtIn.format);
		}
		desc.NumRenderTargets = rhiDesc.renderTargetCount;

		desc.DepthStencilState.DepthEnable = rhiDesc.depthStencil.enableDepthTest ? TRUE : FALSE;
		desc.DepthStencilState.DepthFunc = GetD3DComparisonFunction(rhiDesc.depthStencil.depthComparison);
		desc.DepthStencilState.DepthWriteMask = rhiDesc.depthStencil.enableDepthWrites ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DSVFormat = GetD3DFormat(rhiDesc.depthStencil.depthStencilFormat);
		
		desc.VS.pShaderBytecode = rhiDesc.vertexShader.data;
		desc.VS.BytecodeLength = rhiDesc.vertexShader.byteCount;
		desc.PS.pShaderBytecode = rhiDesc.pixelShader.data;
		desc.PS.BytecodeLength = rhiDesc.pixelShader.byteCount;

		desc.RasterizerState.AntialiasedLineEnable = FALSE;
		desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		desc.RasterizerState.CullMode = GetD3DCullMode(rhiDesc.rasterizer.cullMode);
		desc.RasterizerState.FrontCounterClockwise = FALSE;
		desc.RasterizerState.DepthBias = 0;
		desc.RasterizerState.DepthBiasClamp = 0.0f;
		desc.RasterizerState.SlopeScaledDepthBias = 0.0f;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.ForcedSampleCount = 0;
		desc.RasterizerState.MultisampleEnable = FALSE;

		ID3D12PipelineState* pso;
		D3D(rhi.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
		SetDebugName(pso, rhiDesc.name);

		Pipeline rhiPipeline = { 0 };
		rhiPipeline.type = PipelineType::Graphics;
		rhiPipeline.graphicsDesc = rhiDesc;
		rhiPipeline.pso = pso;

		return rhi.pipelines.Add(rhiPipeline);
	}

	void DestroyPipeline(HPipeline pipeline)
	{
		COM_RELEASE(rhi.pipelines.Get(pipeline).pso);
		rhi.pipelines.Remove(pipeline);
	}

	void CmdBindRootSignature(HRootSignature rootSignature)
	{
		Q_assert(CanWriteCommands());

		const RootSignature sig = rhi.rootSignatures.Get(rootSignature);
		// @TODO: decide between graphics and compute!
		rhi.commandList->SetGraphicsRootSignature(sig.signature);
	}

	void CmdBindPipeline(HPipeline pipeline)
	{
		Q_assert(CanWriteCommands());

		const Pipeline pipe = rhi.pipelines.Get(pipeline);
		rhi.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // @TODO: grab from pipe!
		rhi.commandList->SetPipelineState(pipe.pso);
	}

	void CmdBindVertexBuffers(uint32_t count, const HBuffer* vertexBuffers, const uint32_t* byteStrides, const uint32_t* startByteOffsets)
	{
		Q_assert(CanWriteCommands());
		Q_assert(count <= MaxVertexBufferCount);

		count = min(count, MaxVertexBufferCount);

		D3D12_VERTEX_BUFFER_VIEW views[MaxVertexBufferCount];
		for(uint32_t v = 0; v < count; ++v)
		{
			const Buffer& buffer = rhi.buffers.Get(vertexBuffers[v]);
			const uint32_t offset = startByteOffsets ? startByteOffsets[v] : 0;
			views[v].BufferLocation = buffer.gpuAddress + offset;
			views[v].SizeInBytes = buffer.desc.byteCount - offset;
			views[v].StrideInBytes = byteStrides[v];
		}
		rhi.commandList->IASetVertexBuffers(0, count, views);
	}

	void CmdBindIndexBuffer(HBuffer indexBuffer, IndexType::Id type, uint32_t startByteOffset)
	{
		Q_assert(CanWriteCommands());

		const Buffer& buffer = rhi.buffers.Get(indexBuffer);

		D3D12_INDEX_BUFFER_VIEW view = { 0 };
		view.BufferLocation = buffer.gpuAddress + startByteOffset;
		view.Format = GetD3DIndexFormat(type);
		view.SizeInBytes = (UINT)(buffer.desc.byteCount - startByteOffset);
		rhi.commandList->IASetIndexBuffer(&view);
	}

	void CmdSetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h, float minDepth, float maxDepth)
	{
		Q_assert(CanWriteCommands());

		D3D12_VIEWPORT viewport;
		viewport.TopLeftX = x;
		viewport.TopLeftY = y;
		viewport.Width = w;
		viewport.Height = h;
		viewport.MinDepth = minDepth;
		viewport.MaxDepth = maxDepth;
		rhi.commandList->RSSetViewports(1, &viewport);
	}

	void CmdSetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
	{
		Q_assert(CanWriteCommands());

		D3D12_RECT rect;
		rect.left = x;
		rect.top = y;
		rect.right = x + w;
		rect.bottom = y + h;
		rhi.commandList->RSSetScissorRects(1, &rect);
	}

	void CmdSetRootConstants(HRootSignature rootSignature, ShaderType::Id shaderType, const void* constants)
	{
		Q_assert(CanWriteCommands());
		Q_assert(constants);

		const RootSignature& sig = rhi.rootSignatures.Get(rootSignature);
		const UINT parameterIndex = sig.constants[shaderType].parameterIndex;
		const UINT constantCount = sig.desc.constants[shaderType].count;

		// @TODO: check that the rootSignature specified is already set
		//rhi.commandList->SetGraphicsRootSignature(sig.signature);

		// @TODO: decide between graphics and compute!
		rhi.commandList->SetGraphicsRoot32BitConstants(parameterIndex, constantCount, constants, 0);

		// @TODO: move out, etc
		rhi.commandList->SetGraphicsRootDescriptorTable(sig.firstTableIndex + 0, rhi.descHeapTex2D.mHeap->GetGPUDescriptorHandleForHeapStart());
		rhi.commandList->SetGraphicsRootDescriptorTable(sig.firstTableIndex + 1, rhi.descHeapSamplers.mHeap->GetGPUDescriptorHandleForHeapStart());
	}

	/*void CmdSetRootDescriptorTable(uint32_t tableIndex, HDescriptorTable descriptorTable)
	{
		Q_assert(CanWriteCommands());

		//const DescriptorTable& table = rhi.descriptorTables.Get(descriptorTable);
		//const RootSignature& sig = rhi.rootSignatures.Get(table.rootSignature);
		//const uint32_t index = 2 + tableIndex; // @TODO: grab the real offset

		// @TODO: decide between graphics and compute!
		//rhi.commandList->SetGraphicsRootDescriptorTable(index, );
	}*/

	void CmdDraw(uint32_t vertexCount, uint32_t firstVertex)
	{
		Q_assert(CanWriteCommands());

		rhi.commandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
	}

	void CmdDrawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex)
	{
		Q_assert(CanWriteCommands());

		rhi.commandList->DrawIndexedInstanced(indexCount, 1, firstIndex, firstVertex, 0);
	}

	HDurationQuery CmdBeginDurationQuery(const char* name)
	{
		Q_assert(CanWriteCommands());

		FrameQueries& fq = rhi.frameQueries[rhi.frameIndex];
		Q_assert(fq.durationQueryCount < MaxDurationQueries);
		if(fq.durationQueryCount >= MaxDurationQueries)
		{
			return MAKE_NULL_HANDLE();
		}

		const UINT beginIndex = rhi.durationQueryIndex * 2;
		rhi.commandList->EndQuery(rhi.timeStampHeap, D3D12_QUERY_TYPE_TIMESTAMP, beginIndex);

		DurationQuery& query = fq.durationQueries[fq.durationQueryCount];
		Q_assert(query.state == QueryState::Free);
		Handle type, index, generation;
		DecomposeHandle(&type, &index, &generation, query.handle);
		query.handle = CreateHandle(ResourceType::DurationQuery, fq.durationQueryCount, generation);
		query.queryIndex = rhi.durationQueryIndex;
		query.state = QueryState::Begun;
		Q_strncpyz(query.name, name, sizeof(query.name));

		fq.durationQueryCount++;
		rhi.durationQueryIndex = (rhi.durationQueryIndex + 1) % MaxDurationQueries;

		return MAKE_HANDLE(query.handle);
	}

	void CmdEndDurationQuery(HDurationQuery handle)
	{
		Q_assert(CanWriteCommands());

		Handle type, index, gen;
		DecomposeHandle(&type, &index, &gen, handle.v);

		FrameQueries& fq = rhi.frameQueries[rhi.frameIndex];
		Q_assert(index < fq.durationQueryCount);
		if(index >= fq.durationQueryCount)
		{
			return;
		}

		DurationQuery& query = fq.durationQueries[index];
		Q_assert(handle.v == query.handle);
		if(handle.v != query.handle)
		{
			return;
		}

		Q_assert(query.state == QueryState::Begun);
		const UINT endIndex = query.queryIndex * 2 + 1;
		rhi.commandList->EndQuery(rhi.timeStampHeap, D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
		
		const Buffer& buffer = rhi.buffers.Get(rhi.timeStampBuffer);
		const UINT timeStampIndex = query.queryIndex * 2;
		const UINT64 destIndex = (rhi.frameIndex * MaxDurationQueries * 2) + timeStampIndex;
		const UINT64 destByteOffset = destIndex * sizeof(UINT64);
		rhi.commandList->ResolveQueryData(rhi.timeStampHeap, D3D12_QUERY_TYPE_TIMESTAMP, timeStampIndex, 2, buffer.buffer, destByteOffset);

		query.state = QueryState::Ended;
	}

#if defined(_DEBUG) || defined(CNQ3_DEV)

	static ShaderByteCode CompileShader(const char* source, const char* target)
	{
		// yup, this leaks memory but we don't care as it's for quick and dirty testing
		// could write to a linear allocator instead...
		ID3DBlob* blob;
		ID3DBlob* error;
#if defined(_DEBUG)
		const UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
		if(FAILED(D3DCompile(source, strlen(source), NULL, NULL, NULL, "main", target, flags, 0, &blob, &error)))
		{
			ri.Error(ERR_FATAL, "Shader (%s) compilation failed:\n%s\n", target, (const char*)error->GetBufferPointer());
			return ShaderByteCode();
		}

		ShaderByteCode byteCode;
		byteCode.data = blob->GetBufferPointer();
		byteCode.byteCount = blob->GetBufferSize();

		return byteCode;
	}

	ShaderByteCode CompileVertexShader(const char* source)
	{
		return CompileShader(source, "vs_5_1");
	}

	ShaderByteCode CompilePixelShader(const char* source)
	{
		return CompileShader(source, "ps_5_1");
	}

#endif
}
