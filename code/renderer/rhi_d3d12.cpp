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

- 3D/world rendering
- mip-map generation accounting for r_picmip
- when creating the root signature, validate that neither of the tables have any gap
- use root signature 1.1 to use the hints that help the drivers optimize out static resources
- remove srvIndex or textureIndex from image_t
	can't remove either for now
- is it possible to force Resource Binding Tier 2 somehow? are we supposed to run on old HW to test? :(
	see if WARP allows us to do that?
- don't do persistent mapping to help out RenderDoc?
- implicit barrier & profiling API: Begin/EndRenderPass
* partial inits and shutdown
- clean up the Win32 window creation/update mess
- move as much GUI logic as possible out of the RHI (especially render pass timings)
- leverage rhi.allocator->IsCacheCoherentUMA()
- defragment on partial inits with D3D12MA?
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
notes/plans:

- for our views, we only allow "all mips" and "this single mip", no ranges
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

/*
There is an additional restriction for Tier 1 hardware that applies to all heaps,
and to Tier 2 hardware that applies to CBV and UAV heaps,
that all descriptor heap entries covered by descriptor tables in the root signature
must be populated with descriptors by the time the shader executes,
even if the shader (perhaps due to branching) does not need the descriptor.

There is no such restriction for Tier 3 hardware.
One mitigation for this restriction is the diligent use of Null descriptors.
*/


#include "rhi_local.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#if defined(_DEBUG) || defined(CNQ3_DEV)
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")
#endif
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include "D3D12MemAlloc.h"
#include "../imgui/imgui.h"


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
	static const uint32_t MaxCPUGenericDescriptors = RHI_MAX_TEXTURES_2D * 4;
	static const uint32_t MaxCPUSamplerDescriptors = RHI_MAX_SAMPLERS * 4;
	static const uint32_t MaxCPURTVDescriptors = 256;
	static const uint32_t MaxCPUDSVDescriptors = 64;
	static const uint32_t MaxCPUDescriptors =
		MaxCPUGenericDescriptors +
		MaxCPUSamplerDescriptors +
		MaxCPURTVDescriptors +
		MaxCPUDSVDescriptors;

	struct ResourceType
	{
		enum Id
		{
			// @NOTE: a valid type never being 0 means we can discard 0 handles right away
			Invalid,
			Buffer,
			Texture,
			Sampler,
			RootSignature,
			DescriptorTable,
			Pipeline,
			DurationQuery,
			Count
		};
	};

#define D3D_RESOURCE_LIST(R) \
	R(CommandQueue, "command queue") \
	R(CommandAllocator, "command allocator") \
	R(PipelineState, "pipeline state") \
	R(CommandList, "command list") \
	R(Fence, "fence") \
	R(RootSignature, "root signature") \
	R(DescriptorHeap, "descriptor heap") \
	R(Heap, "heap") \
	R(QueryHeap, "query heap") \
	R(Texture, "texture") \
	R(Buffer, "buffer")

#define R(Enum, Name) Enum,
	struct D3DResourceType
	{
		enum Id
		{
			D3D_RESOURCE_LIST(R)
			Count
		};
	};
#undef R

#define R(Enum, Name) Name,
	static const char* D3DResourceNames[] =
	{
		D3D_RESOURCE_LIST(R)
		""
	};
#undef R

#undef D3D_RESOURCE_LIST

	struct Buffer
	{
		BufferDesc desc;
		D3D12MA::Allocation* allocation;
		ID3D12Resource* buffer;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
		D3D12_RESOURCE_STATES currentState;
		bool mapped;
		bool uploading;
		UINT64 uploadByteOffset;
		bool shortLifeTime = false;
	};

	struct Texture
	{
		TextureDesc desc;
		D3D12MA::Allocation* allocation;
		ID3D12Resource* texture;
		uint32_t srvIndex;
		uint32_t rtvIndex;
		uint32_t dsvIndex;
		D3D12_RESOURCE_STATES currentState;
		struct Mip
		{
			uint32_t uavIndex;
		}
		mips[MaxTextureMips];
		bool uploading;
		uint32_t uploadByteOffset;
		bool shortLifeTime = false;
	};

	struct RootSignature
	{
		struct PerStageConstants
		{
			UINT parameterIndex;
		};
		RootSignatureDesc desc;
		ID3D12RootSignature* signature;
		PerStageConstants constants[ShaderStage::Count];
		UINT genericTableIndex;
		UINT samplerTableIndex;
		UINT genericDescCount;
		UINT samplerDescCount;
		bool shortLifeTime = false;
	};

	struct DescriptorTable
	{
		ID3D12DescriptorHeap* genericHeap; // SRV, CBV, UAV
		ID3D12DescriptorHeap* samplerHeap;
		bool shortLifeTime = false;
	};

	struct Pipeline
	{
		GraphicsPipelineDesc graphicsDesc;
		ComputePipelineDesc computeDesc;
		ID3D12PipelineState* pso = NULL;
		PipelineType::Id type = PipelineType::Graphics;
		bool shortLifeTime = false;
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

	struct Fence
	{
		void Create(UINT64 value, const char* name);
		void Signal(ID3D12CommandQueue* queue, UINT64 value);
		void WaitOnCPU(UINT64 value);
		void WaitOnGPU(ID3D12CommandQueue* queue, UINT64 value);
		bool HasCompleted(UINT64 value);
		void Release();

		ID3D12Fence* fence;
		HANDLE event;
	};

	struct UploadManager
	{
		void Create();
		void Release();
		uint8_t* BeginBufferUpload(HBuffer buffer);
		void EndBufferUpload(HBuffer buffer);
		void BeginTextureUpload(MappedTexture& mappedTexture, HTexture texture);
		void EndTextureUpload(HTexture texture);
		void WaitToStartDrawing(ID3D12CommandQueue* commandQueue);

		ID3D12CommandQueue* commandQueue;
		ID3D12CommandAllocator* commandAllocator;
		ID3D12GraphicsCommandList* commandList;

		HBuffer uploadHBuffer;
		uint32_t bufferByteCount;
		uint32_t bufferByteOffset;
		uint8_t* mappedBuffer;

		Fence fence;
		// fenceValue      : signaled value after last uploaded chunk
		// fenceValueRewind: signaled value after last uploaded chunk before rewinding the write pointer to the start
		UINT64 fenceValue;
		UINT64 fenceValueRewind;

	private:
		void WaitToStartUploading(uint32_t uploadByteCount);
	};

	struct DescriptorHeap
	{
		void Create(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t size, uint16_t* freeListItems, const char* name);
		void Release();
		uint32_t Allocate();
		void Free(uint32_t index);
		D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index);
		uint32_t CreateSRV(ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
		uint32_t CreateUAV(ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);
		uint32_t CreateRTV(ID3D12Resource* resource, D3D12_RENDER_TARGET_VIEW_DESC& desc);
		uint32_t CreateDSV(ID3D12Resource* resource, D3D12_DEPTH_STENCIL_VIEW_DESC& desc);
		uint32_t CreateSampler(D3D12_SAMPLER_DESC& desc);

		StaticFreeList<uint16_t, InvalidDescriptorIndex> freeList;
		ID3D12DescriptorHeap* heap;
		D3D12_CPU_DESCRIPTOR_HANDLE startAddress;
		UINT descriptorSize;
		D3D12_DESCRIPTOR_HEAP_TYPE type;
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
		ID3D12CommandQueue* mainCommandQueue;
		ID3D12CommandQueue* computeCommandQueue;
		IDXGISwapChain3* swapChain;
		HTexture renderTargets[FrameCount];
		ID3D12CommandAllocator* mainCommandAllocators[FrameCount];
		ID3D12GraphicsCommandList* mainCommandList;
		ID3D12CommandAllocator* tempCommandAllocator;
		ID3D12GraphicsCommandList* tempCommandList;
		bool tempCommandListOpen = false;
		ID3D12GraphicsCommandList* commandList; // not owned, don't release it!
		UINT frameIndex;
		Fence mainFence;
		UINT64 mainFenceValues[FrameCount];
		Fence tempFence;
		UINT64 tempFenceValue;
		ID3D12QueryHeap* timeStampHeap;
		HBuffer timeStampBuffer;
		const UINT64* mappedTimeStamps;
		uint32_t durationQueryIndex;
		HRootSignature currentRootSignature;
		HDurationQuery frameDuration;

		uint16_t descriptorFreeListData[MaxCPUDescriptors];
		DescriptorHeap descHeapGeneric;
		DescriptorHeap descHeapSamplers;
		DescriptorHeap descHeapRTVs;
		DescriptorHeap descHeapDSVs;

#define POOL(Type, Size) StaticPool<Type, H##Type, ResourceType::Type, Size>
		POOL(Buffer, 64) buffers;
		POOL(Texture, MAX_DRAWIMAGES * 2) textures;
		POOL(RootSignature, 64) rootSignatures;
		POOL(DescriptorTable, 64) descriptorTables;
		POOL(Pipeline, 64) pipelines;
#undef POOL

#define DESTROY_POOL_LIST(POOL) \
		POOL(buffers, DestroyBuffer) \
		POOL(textures, DestroyTexture) \
		POOL(rootSignatures, DestroyRootSignature) \
		POOL(descriptorTables, DestroyDescriptorTable) \
		POOL(pipelines, DestroyPipeline)

		// null resources, no manual clean-up needed
		HTexture nullTexture; // SRV
		HTexture nullRWTexture; // UAV
		HBuffer nullBuffer; // CBV
		HBuffer nullRWBuffer; // UAV
		HSampler nullSampler;

		byte stringData[64 << 10];
		LinearAllocator stringAllocator;
		UploadManager upload;
		StaticUnorderedArray<HTexture, MAX_DRAWIMAGES> texturesToTransition;
		FrameQueries frameQueries[FrameCount];
		ResolvedQueries resolvedQueries;
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

	static void SetDebugName(ID3D12DeviceChild* resource, const char* resourceName, D3DResourceType::Id resourceType)
	{
		if(resourceName == NULL || (uint32_t)resourceType >= D3DResourceType::Count)
		{
			return;
		}

		const char* const name = va("%s %s", resourceName, D3DResourceNames[resourceType]);

		// ID3D12Object::SetName is a Unicode wrapper for
		// ID3D12Object::SetPrivateData with WKPDID_D3DDebugObjectNameW
		resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(name), name);
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

	static ID3D12DescriptorHeap* CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT size, bool shaderVisible, const char* name)
	{
		if(size == 0)
		{
			return NULL;
		}

		ID3D12DescriptorHeap* heap;
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { 0 };
		heapDesc.Type = type;
		heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapDesc.NumDescriptors = size;
		heapDesc.NodeMask = 0;
		D3D(rhi.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
		SetDebugName(heap, name, D3DResourceType::DescriptorHeap);

		return heap;
	}

	void Fence::Create(UINT64 value, const char* name)
	{
		D3D(rhi.device->CreateFence(value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		SetDebugName(fence, name, D3DResourceType::Fence);

		event = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(event == NULL)
		{
			Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
		}
	}

	void Fence::Signal(ID3D12CommandQueue* queue, UINT64 value)
	{
		D3D(queue->Signal(fence, value));
	}

	void Fence::WaitOnCPU(UINT64 value)
	{
		if(fence->GetCompletedValue() < value)
		{
			D3D(fence->SetEventOnCompletion(value, event));
			WaitForSingleObjectEx(event, INFINITE, FALSE);
		}
	}

	void Fence::WaitOnGPU(ID3D12CommandQueue* queue, UINT64 value)
	{
		D3D(queue->Wait(fence, value));
	}

	bool Fence::HasCompleted(UINT64 value)
	{
		return fence->GetCompletedValue() >= value;
	}

	void Fence::Release()
	{
		CloseHandle(event);
		event = NULL;
		COM_RELEASE(fence);
	}

	void UploadManager::Create()
	{
		BufferDesc bufferDesc("upload", 128 << 20, ResourceStates::CopyDestinationBit);
		bufferDesc.memoryUsage = MemoryUsage::Upload;
		uploadHBuffer = CreateBuffer(bufferDesc);
		bufferByteCount = bufferDesc.byteCount;
		bufferByteOffset = 0;
		mappedBuffer = MapBuffer(uploadHBuffer);

		D3D12_COMMAND_QUEUE_DESC queueDesc = { 0 };
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.NodeMask = 0;
		D3D(rhi.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
		SetDebugName(commandQueue, "upload", D3DResourceType::CommandQueue);

		D3D(rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&commandAllocator)));
		SetDebugName(commandAllocator, "upload", D3DResourceType::CommandAllocator);

		D3D(rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, commandAllocator, NULL, IID_PPV_ARGS(&commandList)));
		SetDebugName(commandList, "upload", D3DResourceType::CommandList);
		D3D(commandList->Close());

		fence.Create(0, "upload");
		fenceValue = 0;
		fenceValueRewind = 0;
	}

	void UploadManager::Release()
	{
		UnmapBuffer(uploadHBuffer);
		fence.Release();
		COM_RELEASE(commandQueue);
		COM_RELEASE(commandList);
		COM_RELEASE(commandAllocator);
	}

	uint8_t* UploadManager::BeginBufferUpload(HBuffer userHBuffer)
	{
		Buffer& userBuffer = rhi.buffers.Get(userHBuffer);
		Q_assert(!userBuffer.uploading);

		const uint32_t uploadByteCount = userBuffer.desc.byteCount;
		WaitToStartUploading(uploadByteCount);

		uint8_t* mapped = NULL;
		Q_assert(userBuffer.desc.memoryUsage != MemoryUsage::Readback);
		if(userBuffer.desc.memoryUsage == MemoryUsage::GPU)
		{
			mapped = mappedBuffer + bufferByteOffset;
			userBuffer.uploadByteOffset = bufferByteOffset;
		}
		else
		{
			mapped = (uint8_t*)MapBuffer(userHBuffer);
		}

		userBuffer.uploading = true;
		bufferByteOffset = AlignUp(bufferByteOffset + uploadByteCount, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

		return mapped;
	}

	void UploadManager::EndBufferUpload(HBuffer userHBuffer)
	{
		Buffer& userBuffer = rhi.buffers.Get(userHBuffer);
		Q_assert(userBuffer.uploading);

		Buffer& uploadBuffer = rhi.buffers.Get(uploadHBuffer);

		if(!userBuffer.mapped)
		{
			D3D(commandList->Reset(commandAllocator, NULL));

			const UINT64 byteCount = min(userBuffer.desc.byteCount, uploadBuffer.desc.byteCount);
			commandList->CopyBufferRegion(userBuffer.buffer, 0, uploadBuffer.buffer, userBuffer.uploadByteOffset, byteCount);

			ID3D12CommandList* commandLists[] = { commandList };
			D3D(commandList->Close());
			commandQueue->ExecuteCommandLists(ARRAY_LEN(commandLists), commandLists);
			fenceValue++;
			commandQueue->Signal(fence.fence, fenceValue);
		}
		else
		{
			UnmapBuffer(userHBuffer);
		}

		userBuffer.uploading = false;
	}

	void UploadManager::BeginTextureUpload(MappedTexture& mappedTexture, HTexture htexture)
	{
		Texture& texture = rhi.textures.Get(htexture);
		Q_assert(!texture.uploading);
		Q_assert(texture.desc.format == TextureFormat::RGBA32_UNorm); // otherwise the pitch is computed wrong!
		
		const D3D12_RESOURCE_DESC textureDesc = texture.texture->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
		UINT64 uploadByteCount;
		rhi.device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, NULL, NULL, &uploadByteCount);
		WaitToStartUploading(uploadByteCount);

		mappedTexture.mappedData = mappedBuffer + bufferByteOffset;
		mappedTexture.rowCount = texture.desc.height;
		mappedTexture.srcRowByteCount = texture.desc.width * 4;
		mappedTexture.dstRowByteCount = AlignUp(layout.Footprint.RowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

		texture.uploadByteOffset = bufferByteOffset;
		texture.uploading = true;
		bufferByteOffset = AlignUp(bufferByteOffset + uploadByteCount, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	}

	void UploadManager::EndTextureUpload(HTexture htexture)
	{
		Texture& texture = rhi.textures.Get(htexture);
		Q_assert(texture.uploading);

		const D3D12_RESOURCE_DESC textureDesc = texture.texture->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
		rhi.device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, NULL, NULL, NULL);

		Buffer& buffer = rhi.buffers.Get(uploadHBuffer);
		D3D12_TEXTURE_COPY_LOCATION dstLoc = { 0 };
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { 0 };
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.pResource = texture.texture;
		dstLoc.SubresourceIndex = 0;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.pResource = buffer.buffer;
		srcLoc.PlacedFootprint = layout;
		srcLoc.PlacedFootprint.Offset = texture.uploadByteOffset;
		D3D12_BOX srcBox = { 0 };
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = textureDesc.Width;
		srcBox.bottom = textureDesc.Height;
		srcBox.back = 1;

		D3D(commandList->Reset(commandAllocator, NULL));

		commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

		ID3D12CommandList* commandLists[] = { commandList };
		D3D(commandList->Close());
		commandQueue->ExecuteCommandLists(ARRAY_LEN(commandLists), commandLists);
		fenceValue++;
		commandQueue->Signal(fence.fence, fenceValue);

		texture.uploading = false;

		// @TODO: let the user issue the barrier
		/*if(desc.x == 0 &&
			desc.y == 0 &&
			desc.width == texture.desc.width &&
			desc.height && texture.desc.height)
		{
			rhi.texturesToTransition.Add(htexture);
		}*/
	}

	void UploadManager::WaitToStartDrawing(ID3D12CommandQueue* commandQueue_)
	{
		fence.WaitOnGPU(commandQueue_, fenceValue);
	}

	void UploadManager::WaitToStartUploading(uint32_t uploadByteCount)
	{
		if(uploadByteCount > bufferByteCount)
		{
			ri.Error(ERR_FATAL, "Upload request too large!\n");
		}

		if(bufferByteOffset + uploadByteCount > bufferByteCount)
		{
			// not enough space left, force a wait and rewind
			fence.WaitOnCPU(fenceValueRewind);
			D3D(commandAllocator->Reset());
			fenceValueRewind = fenceValue;
			bufferByteOffset = 0;
		}
	}

	void DescriptorHeap::Create(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t size, uint16_t* freeListItems, const char* name)
	{
		heap = CreateDescriptorHeap(heapType, size, false, name);
		freeList.Init(freeListItems, size);
		startAddress = heap->GetCPUDescriptorHandleForHeapStart();
		descriptorSize = rhi.device->GetDescriptorHandleIncrementSize(heapType);
		type = heapType;
	}

	void DescriptorHeap::Release()
	{
		COM_RELEASE(heap);
	}

	uint32_t DescriptorHeap::Allocate()
	{
		return freeList.Allocate();
	}

	void DescriptorHeap::Free(uint32_t index)
	{
		freeList.Free(index);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCPUHandle(uint32_t index)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = startAddress;
		handle.ptr += index * descriptorSize;

		return handle;
	}

	uint32_t DescriptorHeap::CreateSRV(ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
	{
		Q_assert(resource);
		Q_assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		const uint32_t index = freeList.Allocate();
		rhi.device->CreateShaderResourceView(resource, &desc, GetCPUHandle(index));

		return index;
	}

	uint32_t DescriptorHeap::CreateUAV(ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC& desc)
	{
		Q_assert(resource);
		Q_assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		const uint32_t index = freeList.Allocate();
		rhi.device->CreateUnorderedAccessView(resource, NULL, &desc, GetCPUHandle(index));

		return index;
	}

	uint32_t DescriptorHeap::CreateRTV(ID3D12Resource* resource, D3D12_RENDER_TARGET_VIEW_DESC& desc)
	{
		Q_assert(resource);
		Q_assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		const uint32_t index = freeList.Allocate();
		rhi.device->CreateRenderTargetView(resource, &desc, GetCPUHandle(index));

		return index;
	}

	uint32_t DescriptorHeap::CreateDSV(ID3D12Resource* resource, D3D12_DEPTH_STENCIL_VIEW_DESC& desc)
	{
		Q_assert(resource);
		Q_assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		const uint32_t index = freeList.Allocate();
		rhi.device->CreateDepthStencilView(resource, &desc, GetCPUHandle(index));

		return index;
	}

	uint32_t DescriptorHeap::CreateSampler(D3D12_SAMPLER_DESC& desc)
	{
		Q_assert(type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		const uint32_t index = freeList.Allocate();
		rhi.device->CreateSampler(&desc, GetCPUHandle(index));

		return index;
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
		const UINT64 currentFenceValue = rhi.mainFenceValues[rhi.frameIndex];
		rhi.mainFence.Signal(rhi.mainCommandQueue, currentFenceValue);
		rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();
		rhi.mainFence.WaitOnCPU(rhi.mainFenceValues[rhi.frameIndex]);
		rhi.mainFenceValues[rhi.frameIndex] = currentFenceValue + 1;
	}

	static void WaitUntilDeviceIsIdle()
	{
		rhi.mainFence.Signal(rhi.mainCommandQueue, rhi.mainFenceValues[rhi.frameIndex]);
		rhi.mainFence.WaitOnCPU(rhi.mainFenceValues[rhi.frameIndex]);
		rhi.mainFenceValues[rhi.frameIndex]++;
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
		// @TODO: check that the command list is open
		return rhi.commandList != NULL;
	}

	template<typename T, typename HT, Handle RT, int N>
	static void DestroyPool(StaticPool<T, HT, RT, N>& pool, void (*DestroyResource)(HT), bool fullShutDown)
	{
		T* resource;
		HT handle;
		for(int i = 0; pool.FindNext(&resource, &handle, &i);)
		{
			if(fullShutDown || resource->shortLifeTime)
			{
				(*DestroyResource)(handle);
			}
		}

		if(fullShutDown)
		{
			pool.Clear();
		}
	}

	static DXGI_FORMAT GetD3DIndexFormat(IndexType::Id type)
	{
		return type == IndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	}

	static D3D12_SHADER_VISIBILITY GetD3DVisibility(ShaderStage::Id shaderType)
	{
		switch(shaderType)
		{
			case ShaderStage::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
			case ShaderStage::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
			case ShaderStage::Compute: return D3D12_SHADER_VISIBILITY_ALL; // @TODO: assert here too?
			default: Q_assert(!"Unsupported shader type"); return D3D12_SHADER_VISIBILITY_ALL;
		}
	}

	static D3D12_SHADER_VISIBILITY GetD3DVisibility(ShaderStages::Flags flags)
	{
		if(__popcnt(flags & ShaderStages::AllGraphicsBits) > 1)
		{
			return D3D12_SHADER_VISIBILITY_ALL;
		}

		if(flags & ShaderStages::VertexBit)
		{
			return D3D12_SHADER_VISIBILITY_VERTEX;
		}

		if(flags & ShaderStages::PixelBit)
		{
			return D3D12_SHADER_VISIBILITY_PIXEL;
		}

		return D3D12_SHADER_VISIBILITY_ALL;
	}

	static D3D12_DESCRIPTOR_RANGE_TYPE GetD3DDescriptorRangeType(DescriptorType::Id descType)
	{
		switch(descType)
		{
			case DescriptorType::Texture: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			case DescriptorType::Buffer: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			case DescriptorType::RWTexture: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case DescriptorType::RWBuffer: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case DescriptorType::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			default: Q_assert(!"Unsupported descriptor type"); return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
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
			case TextureFormat::RGBA64_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
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

	static D3D12_TEXTURE_ADDRESS_MODE GetD3DTextureAddressMode(textureWrap_t wrap)
	{
		switch(wrap)
		{
			case TW_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			case TW_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			default: Q_assert(!"Unsupported texture wrap mode"); return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		}
	}

	static D3D12_FILTER GetD3DFilter(TextureFilter::Id filter)
	{
		switch(filter)
		{
			case TextureFilter::Point: return D3D12_FILTER_MIN_MAG_MIP_POINT;
			case TextureFilter::Linear: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			case TextureFilter::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
			default: Q_assert(!"Unsupported texture filter mode"); return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}
	}

	static D3D12_RESOURCE_STATES GetD3DResourceStates(ResourceStates::Flags flags)
	{
#define ADD_BITS(RHIBit, D3DBits) \
		if(flags & ResourceStates::RHIBit) \
		{ \
			states |= D3DBits; \
		}

		D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON;
		ADD_BITS(VertexBufferBit, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		ADD_BITS(IndexBufferBit, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		ADD_BITS(ConstantBufferBit, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		ADD_BITS(RenderTargetBit, D3D12_RESOURCE_STATE_RENDER_TARGET);
		ADD_BITS(VertexShaderAccessBit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		ADD_BITS(PixelShaderAccessBit, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		ADD_BITS(ComputeShaderAccessBit, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		ADD_BITS(CopySourceBit, D3D12_RESOURCE_STATE_COPY_SOURCE);
		ADD_BITS(CopyDestinationBit, D3D12_RESOURCE_STATE_COPY_DEST);
		ADD_BITS(DepthReadBit, D3D12_RESOURCE_STATE_DEPTH_READ);
		ADD_BITS(DepthWriteBit, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		ADD_BITS(UnorderedAccessBit, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		ADD_BITS(PresentBit, D3D12_RESOURCE_STATE_PRESENT);

		return states;

#undef ADD_BITS
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

	static bool IsD3DDepthFormat(DXGI_FORMAT format)
	{
		switch(format)
		{
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
				return true;
			default:
				return false;
		}
	}

	static const char* GetNameForD3DResourceStates(D3D12_RESOURCE_STATES states)
	{
		switch(states)
		{
			case D3D12_RESOURCE_STATE_COMMON: return "common/present";
			case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER: return "vertex/constant buffer";
			case D3D12_RESOURCE_STATE_INDEX_BUFFER: return "index buffer";
			case D3D12_RESOURCE_STATE_RENDER_TARGET: return "render target";
			case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return "UAV";
			case D3D12_RESOURCE_STATE_DEPTH_WRITE: return "depth write";
			case D3D12_RESOURCE_STATE_DEPTH_READ: return "depth read";
			case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return "non-pixel shader resource";
			case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return "pixel shader resource";
			case D3D12_RESOURCE_STATE_COPY_DEST: return "copy destination";
			case D3D12_RESOURCE_STATE_COPY_SOURCE: return "copy source";
			case D3D12_RESOURCE_STATE_GENERIC_READ: return "generic read";
			case D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE: return "generic shader resource";
			default: return "???";
		}
	}

	static void ValidateResourceStateForBarrier(D3D12_RESOURCE_STATES state)
	{
		if(state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ||
			state == D3D12_RESOURCE_STATE_DEPTH_WRITE)
		{
			return;
		}

		const D3D12_RESOURCE_STATES readOnly[] =
		{
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
			D3D12_RESOURCE_STATE_INDEX_BUFFER,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_DEPTH_READ
		};
		const D3D12_RESOURCE_STATES readWrite[] =
		{
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		};
		const D3D12_RESOURCE_STATES writeOnly[] =
		{
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_STREAM_OUT
		};

		int rBits = 0;
		int wBits = 0;

		for(auto bit : readOnly)
		{
			if(state & bit)
			{
				rBits++;
			}
		}
		for(auto bit : readWrite)
		{
			if(state & bit)
			{
				rBits++;
				wBits++;
			}
		}
		for(auto bit : writeOnly)
		{
			if(state & bit)
			{
				wBits++;
			}
		}

		// MS: "At most one write bit can be set."
		Q_assert(wBits == 0 || wBits == 1);

		if(wBits == 1)
		{
			// MS: "If any write bit is set, then no read bit may be set."
			Q_assert(rBits == 0);
		}
	}

	// returns true if the barrier should be used
	static bool SetBarrier(
		D3D12_RESOURCE_STATES& currentState, D3D12_RESOURCE_BARRIER& barrier,
		ResourceStates::Flags newState, ID3D12Resource* resource)
	{
		const D3D12_RESOURCE_STATES before = currentState;
		const D3D12_RESOURCE_STATES after = GetD3DResourceStates(newState);
		ValidateResourceStateForBarrier(before);
		ValidateResourceStateForBarrier(after);

		if(before & after & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		{
			// note that UAV barriers are unnecessary in a bunch of cases:
			// - before/after access is read-only
			// - before/after access is write-only, but to different ranges
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = resource;
		}
		else
		{
			if(before == after)
			{
				return false;
			}

			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource;
			barrier.Transition.StateBefore = before;
			barrier.Transition.StateAfter = after;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			currentState = after;
		}

		return true;
	}

	static void ResolveDurationQueries()
	{
		UINT64 gpuFrequency;
		D3D(rhi.mainCommandQueue->GetTimestampFrequency(&gpuFrequency));
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

	static void GrabSwapChainTextures()
	{
		for(uint32_t f = 0; f < FrameCount; ++f)
		{
			ID3D12Resource* renderTarget;
			D3D(rhi.swapChain->GetBuffer(f, IID_PPV_ARGS(&renderTarget)));

			TextureDesc desc(va("swap chain #%d", f + 1), glConfig.vidWidth, glConfig.vidHeight);
			desc.nativeResource = renderTarget;
			desc.initialState = ResourceStates::PresentBit;
			desc.allowedState = ResourceStates::PresentBit | ResourceStates::RenderTargetBit;
			rhi.renderTargets[f] = CreateTexture(desc);
		}
	}

	static void CreateNullResources()
	{
		{
			TextureDesc desc("null", 1, 1);
			rhi.nullTexture = CreateTexture(desc);
		}
		{
			TextureDesc desc("null RW", 1, 1);
			desc.format = TextureFormat::RGBA32_UNorm;
			desc.initialState = ResourceStates::UnorderedAccessBit;
			desc.allowedState = ResourceStates::UnorderedAccessBit | ResourceStates::PixelShaderAccessBit;
			rhi.nullRWTexture = CreateTexture(desc);
		}
		{
			BufferDesc desc("null", 256, ResourceStates::ShaderAccessBits);
			desc.memoryUsage = MemoryUsage::GPU;
			rhi.nullBuffer = CreateBuffer(desc);
		}
		{
			BufferDesc desc("null RW", 256, ResourceStates::UnorderedAccessBit);
			desc.memoryUsage = MemoryUsage::GPU;
			rhi.nullRWBuffer = CreateBuffer(desc);
		}
		rhi.nullSampler = CreateSampler(SamplerDesc());
	}

	static void CopyDescriptor(ID3D12DescriptorHeap* dstHeap, uint32_t dstIndex, DescriptorHeap& srcHeap, uint32_t srcIndex)
	{
		Q_assert(srcIndex != InvalidDescriptorIndex);
		D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = dstHeap->GetCPUDescriptorHandleForHeapStart();
		dstHandle.ptr += dstIndex * srcHeap.descriptorSize;
		rhi.device->CopyDescriptorsSimple(1, dstHandle, srcHeap.GetCPUHandle(srcIndex), srcHeap.type);
	}

	static bool BeginTable(const char* name, int count)
	{
		ImGui::Text(name);

		return ImGui::BeginTable(name, count, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable);
	}

	static void TableHeader(int count, ...)
	{
		va_list args;
		va_start(args, count);
		for(int i = 0; i < count; ++i)
		{
			const char* header = va_arg(args, const char*);
			ImGui::TableSetupColumn(header);
		}
		va_end(args);

		ImGui::TableHeadersRow();
	}

	static void TableRow(int count, ...)
	{
		ImGui::TableNextRow();

		va_list args;
		va_start(args, count);
		for(int i = 0; i < count; ++i)
		{
			const char* item = va_arg(args, const char*);
			ImGui::TableSetColumnIndex(i);
			ImGui::Text(item);
		}
		va_end(args);
	}

	static void TableRow2Bool(const char* item0, bool item1)
	{
		TableRow(2, item0, item1 ? "YES" : "NO");
	}

	static void DrawPerfStats()
	{
		if(BeginTable("GPU timings", 2))
		{
			TableHeader(2, "Pass", "Micro-seconds");

			const ResolvedQueries& rq = rhi.resolvedQueries;
			for(uint32_t q = 0; q < rq.durationQueryCount; ++q)
			{
				const ResolvedDurationQuery& rdq = rq.durationQueries[q];
				TableRow(2, rdq.name, va("%d", (int)rdq.gpuMicroSeconds));
			}

			ImGui::EndTable();
		}
	}

	static void DrawResourceUsage()
	{
		if(BeginTable("Handles", 2))
		{
			TableHeader(2, "Type", "Count");

#define ITEM(Name, Variable) TableRow(2, Name, va("%d", (int)Variable.CountUsedSlots()))
			ITEM("Buffers", rhi.buffers);
			ITEM("Textures", rhi.textures);
			ITEM("Root Signatures", rhi.rootSignatures);
			ITEM("Descriptor Tables", rhi.descriptorTables);
			ITEM("Pipelines", rhi.pipelines);
#undef ITEM

			ImGui::EndTable();
		}

		if(BeginTable("Descriptors", 2))
		{
			TableHeader(2, "Type", "Count");

#define ITEM(Name, Variable) TableRow(2, Name, va("%d", (int)Variable.allocatedItemCount))
			ITEM("CBV/SRV/UAV", rhi.descHeapGeneric.freeList);
			ITEM("Samplers", rhi.descHeapSamplers.freeList);
			ITEM("RTV", rhi.descHeapRTVs.freeList);
			ITEM("DSV", rhi.descHeapDSVs.freeList);
#undef ITEM

			ImGui::EndTable();
		}

		if(BeginTable("Memory", 2))
		{
			D3D12MA::Budget budget;
			rhi.allocator->GetBudget(&budget, NULL);
			TableRow2Bool("UMA", rhi.allocator->IsUMA());
			TableRow2Bool("Cache coherent UMA", rhi.allocator->IsCacheCoherentUMA());
			TableRow(2, "Total", Com_FormatBytes(rhi.allocator->GetMemoryCapacity(DXGI_MEMORY_SEGMENT_GROUP_LOCAL)));
			TableRow(2, "Budget", Com_FormatBytes(budget.BudgetBytes));
			TableRow(2, "Usage", Com_FormatBytes(budget.UsageBytes));
			TableRow(2, "Allocated", Com_FormatBytes(budget.Stats.BlockBytes));
			TableRow(2, "Used", Com_FormatBytes(budget.Stats.AllocationBytes));
			TableRow(2, "Block count", va("%d", budget.Stats.BlockCount));
			TableRow(2, "Allocation count", va("%d", budget.Stats.AllocationCount));

			ImGui::EndTable();
		}
	}

	static void DrawCaps()
	{
		if(BeginTable("Capabilities", 2))
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS options0 = { 0 };
			if(SUCCEEDED(rhi.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options0, sizeof(options0))))
			{
				const char* tier = "Unknown";
				switch(options0.ResourceBindingTier)
				{
					case D3D12_RESOURCE_BINDING_TIER_1: tier = "1"; break;
					case D3D12_RESOURCE_BINDING_TIER_2: tier = "2"; break;
					case D3D12_RESOURCE_BINDING_TIER_3: tier = "3"; break;
					default: break;
				}
				TableRow(2, "Resource binding tier", tier);
			}

			D3D12_FEATURE_DATA_ARCHITECTURE arch0 = { 0 };
			if(SUCCEEDED(rhi.device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch0, sizeof(arch0))))
			{
				TableRow2Bool("Tile-based renderer", arch0.TileBasedRenderer);
			}

			D3D12_FEATURE_DATA_ROOT_SIGNATURE root0 = {};
			if(SUCCEEDED(rhi.device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root0, sizeof(root0))))
			{
				const char* version = "Unknown";
				switch(root0.HighestVersion)
				{
					case D3D_ROOT_SIGNATURE_VERSION_1_0: version = "1.0";
					case D3D_ROOT_SIGNATURE_VERSION_1_1: version = "1.1";
					default: break;
				}
				TableRow(2, "Root signature version", version);
			}

			D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = { 0 };
			if(SUCCEEDED(rhi.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
			{
				const char* tier = "Unknown";
				switch(options5.RenderPassesTier)
				{
					case D3D12_RENDER_PASS_TIER_0: tier = "0";
					case D3D12_RENDER_PASS_TIER_1: tier = "1";
					case D3D12_RENDER_PASS_TIER_2: tier = "2";
					default: break;
				}
				TableRow(2, "Render passes tier", tier);

				tier = "Unknown";
				switch(options5.RaytracingTier)
				{
					case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: tier = "Not supported";
					case D3D12_RAYTRACING_TIER_1_0: tier = "1.0";
					case D3D12_RAYTRACING_TIER_1_1: tier = "1.1";
					default: break;
				}
				TableRow(2, "Raytracing tier (DXR)", tier);
			}

			ImGui::EndTable();
		}
	}

	static void DrawTextures()
	{
		static char filter[256];
		if(ImGui::Button("Clear filter"))
		{
			filter[0] = '\0';
		}
		ImGui::SameLine();
		ImGui::InputText(" ", filter, ARRAY_LEN(filter));

		if(BeginTable("Textures", 2))
		{
			TableHeader(2, "Name", "State");

			int i = 0;
			Texture* texture;
			HTexture htexture;
			while(rhi.textures.FindNext(&texture, &htexture, &i))
			{
				if(filter[0] != '\0' && !Com_Filter(filter, texture->desc.name))
				{
					continue;
				}
				TableRow(2, texture->desc.name, GetNameForD3DResourceStates(texture->currentState));
			}

			ImGui::EndTable();
		}
	}

	typedef void (*UICallback)();

	static void DrawSection(const char* name, UICallback callback)
	{
		if(ImGui::BeginTabItem(name))
		{
			(*callback)();
			ImGui::EndTabItem();
		}
	}

	static void DrawGUI()
	{
		if(ImGui::Begin("Direct3D 12 RHI"))
		{
			ImGui::BeginTabBar("Tabs#RHI");
			DrawSection("Performance", &DrawPerfStats);
			DrawSection("Resources", &DrawResourceUsage);
			DrawSection("Caps", &DrawCaps);
			DrawSection("Textures", &DrawTextures);
			ImGui::EndTabBar();
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
				
				D3D(rhi.swapChain->ResizeBuffers(desc.BufferCount, glConfig.vidWidth, glConfig.vidHeight, desc.BufferDesc.Format, desc.Flags));				

				GrabSwapChainTextures();

				rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();

				for(uint32_t f = 0; f < FrameCount; ++f)
				{
					rhi.mainFenceValues[f] = 0;
				}
				rhi.mainFenceValues[rhi.frameIndex]++;
			}

			return;
		}

		// @NOTE: we can't use memset because of the StaticPool members
		new (&rhi) RHIPrivate();

		rhi.stringAllocator.Init(rhi.stringData, sizeof(rhi.stringData));

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

		{
			uint16_t* freeList = rhi.descriptorFreeListData;
			rhi.descHeapGeneric.Create(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MaxCPUGenericDescriptors, freeList, "all-encompassing CBV SRV UAV");
			freeList += MaxCPUGenericDescriptors;
			rhi.descHeapSamplers.Create(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, MaxCPUSamplerDescriptors, freeList, "all-encompassing sampler");
			freeList += MaxCPUSamplerDescriptors;
			rhi.descHeapRTVs.Create(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, MaxCPURTVDescriptors, freeList, "all-encompassing RTV");
			freeList += MaxCPURTVDescriptors;
			rhi.descHeapDSVs.Create(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, MaxCPUDSVDescriptors, freeList, "all-encompassing DSV");

			// @TODO: remove test
			rhi.descHeapRTVs.Allocate();
		}

		{
			D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { 0 };
			commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			commandQueueDesc.NodeMask = 0;
			D3D(rhi.device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&rhi.mainCommandQueue)));
			SetDebugName(rhi.mainCommandQueue, "main", D3DResourceType::CommandQueue);

			commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			D3D(rhi.device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&rhi.computeCommandQueue)));
			SetDebugName(rhi.computeCommandQueue, "compute", D3DResourceType::CommandQueue);
		}

		{
			IDXGISwapChain* dxgiSwapChain;
			DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
			swapChainDesc.BufferCount = FrameCount;
			swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.BufferDesc.Width = glConfig.vidWidth;
			swapChainDesc.BufferDesc.Height = glConfig.vidHeight;
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
			D3D(rhi.factory->CreateSwapChain(rhi.mainCommandQueue, &swapChainDesc, &dxgiSwapChain));

			D3D(dxgiSwapChain->QueryInterface(IID_PPV_ARGS(&rhi.swapChain)));
			rhi.frameIndex = rhi.swapChain->GetCurrentBackBufferIndex();
			COM_RELEASE(dxgiSwapChain);

			GrabSwapChainTextures();
		}

		for(UINT f = 0; f < FrameCount; ++f)
		{
			D3D(rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rhi.mainCommandAllocators[f])));
			SetDebugName(rhi.mainCommandAllocators[f], va("main #%d", f + 1), D3DResourceType::CommandAllocator);
		}

		// get command list ready to use during init
		D3D(rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rhi.mainCommandAllocators[rhi.frameIndex], NULL, IID_PPV_ARGS(&rhi.mainCommandList)));
		SetDebugName(rhi.mainCommandList, "main", D3DResourceType::CommandList);

		D3D(rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&rhi.tempCommandAllocator)));
		SetDebugName(rhi.tempCommandAllocator, "temp", D3DResourceType::CommandAllocator);

		// the temp command list is always left open for the user
		D3D(rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, rhi.tempCommandAllocator, NULL, IID_PPV_ARGS(&rhi.tempCommandList)));
		SetDebugName(rhi.tempCommandList, "temp", D3DResourceType::CommandList);
		rhi.tempCommandListOpen = true;

		// the active/bound command list is the main one by default
		rhi.commandList = rhi.mainCommandList;

		rhi.mainFence.Create(rhi.mainFenceValues[rhi.frameIndex], "main command queue");
		rhi.mainFenceValues[rhi.frameIndex]++;

		rhi.tempFence.Create(rhi.tempFenceValue, "temp command queue");

		rhi.upload.Create();

		{
			rhi.durationQueryIndex = 0;
			D3D12_QUERY_HEAP_DESC desc = { 0 };
			desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			desc.Count = MaxDurationQueries * 2;
			desc.NodeMask = 0;
			D3D(rhi.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&rhi.timeStampHeap)));
			SetDebugName(rhi.timeStampHeap, "timestamp", D3DResourceType::QueryHeap);
		}

		{
			const uint32_t byteCount = MaxDurationQueries * 2 * FrameCount * sizeof(UINT64);
			BufferDesc desc("timestamp readback", byteCount, ResourceStates::CopySourceBit);
			desc.memoryUsage = MemoryUsage::Readback;
			rhi.timeStampBuffer = CreateBuffer(desc);
			rhi.mappedTimeStamps = (UINT64*)MapBuffer(rhi.timeStampBuffer);
		}

		CreateNullResources();

		// queue some actual work...

		D3D(rhi.commandList->Close());

		WaitUntilDeviceIsIdle();

		glInfo.maxTextureSize = MAX_TEXTURE_SIZE;
		glInfo.maxAnisotropy = 16;
		glInfo.depthFadeSupport = qfalse;
	}

	void ShutDown(qbool destroyWindow)
	{
#define DESTROY_POOL(Name, Func) DestroyPool(rhi.Name, &Func, !!destroyWindow);

		if(!destroyWindow)
		{
			WaitUntilDeviceIsIdle();

			rhi.texturesToTransition.Clear();

			DESTROY_POOL_LIST(DESTROY_POOL);

			return;
		}

		WaitUntilDeviceIsIdle();

		rhi.upload.Release();
		rhi.mainFence.Release();
		rhi.tempFence.Release();
		rhi.descHeapGeneric.Release();
		rhi.descHeapSamplers.Release();
		rhi.descHeapRTVs.Release();
		rhi.descHeapDSVs.Release();

		DESTROY_POOL_LIST(DESTROY_POOL);

		COM_RELEASE(rhi.timeStampHeap);
		COM_RELEASE(rhi.mainCommandList);
		COM_RELEASE_ARRAY(rhi.mainCommandAllocators);
		COM_RELEASE(rhi.tempCommandList);
		COM_RELEASE(rhi.tempCommandAllocator);
		COM_RELEASE(rhi.swapChain);
		COM_RELEASE(rhi.computeCommandQueue);
		COM_RELEASE(rhi.mainCommandQueue);
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

	static void WaitForTempCommandList()
	{
		rhi.tempFence.WaitOnCPU(rhi.tempFenceValue);
		if(rhi.tempCommandListOpen)
		{
			rhi.tempCommandList->Close();
		}
		D3D(rhi.tempCommandAllocator->Reset());
		D3D(rhi.tempCommandList->Reset(rhi.tempCommandAllocator, NULL));
		rhi.tempCommandListOpen = true;
	}

	void BeginFrame()
	{
		Q_assert(rhi.commandList == rhi.mainCommandList);

		rhi.currentRootSignature = RHI_MAKE_NULL_HANDLE();

		WaitForTempCommandList();

		// wait for pending copies from the upload manager to be finished
		rhi.upload.WaitToStartDrawing(rhi.mainCommandQueue);

		// reclaim used memory and start recording
		D3D(rhi.mainCommandAllocators[rhi.frameIndex]->Reset());
		D3D(rhi.commandList->Reset(rhi.mainCommandAllocators[rhi.frameIndex], NULL));

		rhi.frameDuration = CmdBeginDurationQuery("Whole frame");

		const TextureBarrier barrier(rhi.renderTargets[rhi.frameIndex], ResourceStates::RenderTargetBit);
		CmdBarrier(1, &barrier);

		const uint32_t rtvIndex = rhi.textures.Get(rhi.renderTargets[rhi.frameIndex]).rtvIndex;
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rhi.descHeapRTVs.GetCPUHandle(rtvIndex);
		rhi.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

		const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		rhi.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);

		static TextureBarrier barriers[MAX_DRAWIMAGES];
		for(uint32_t t = 0; t < rhi.texturesToTransition.count; ++t)
		{
			const HTexture handle = rhi.texturesToTransition[t];
			const Texture& texture = rhi.textures.Get(handle);
			barriers[t] = TextureBarrier(handle, texture.desc.initialState);
		}
		CmdBarrier(rhi.texturesToTransition.count, barriers);
		rhi.texturesToTransition.Clear();
	}

	void EndFrame()
	{
		const TextureBarrier barrier(rhi.renderTargets[rhi.frameIndex], ResourceStates::PresentBit);
		CmdBarrier(1, &barrier);

		CmdEndDurationQuery(rhi.frameDuration);

		// stop recording
		D3D(rhi.commandList->Close());

		ID3D12CommandList* commandListArray[] = { rhi.commandList };
		rhi.mainCommandQueue->ExecuteCommandLists(ARRAY_LEN(commandListArray), commandListArray);

		Present();

		MoveToNextFrame();

		ResolveDurationQueries();
	}

	uint32_t GetFrameIndex()
	{
		return rhi.frameIndex;
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
		if(rhiDesc.initialState & ResourceStates::UnorderedAccessBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
		D3D12MA::ALLOCATION_DESC allocDesc = { 0 };
		allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		if(rhiDesc.memoryUsage == MemoryUsage::CPU || rhiDesc.memoryUsage == MemoryUsage::Upload)
		{
			allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
			resourceState = D3D12_RESOURCE_STATE_GENERIC_READ; // mandated
		}
		else if(rhiDesc.memoryUsage == MemoryUsage::Readback)
		{
			allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
			resourceState = D3D12_RESOURCE_STATE_COPY_DEST; // mandated
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
		((BufferDesc&)rhiDesc).name = rhi.stringAllocator.Allocate(rhiDesc.name);
		SetDebugName(resource, rhiDesc.name, D3DResourceType::Buffer);

		Buffer buffer = {};
		buffer.desc = rhiDesc;
		buffer.allocation = allocation;
		buffer.buffer = resource;
		buffer.gpuAddress = resource->GetGPUVirtualAddress();
		buffer.currentState = resourceState;
		buffer.shortLifeTime = rhiDesc.shortLifeTime;

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

	uint8_t* MapBuffer(HBuffer handle)
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

		return (uint8_t*)mappedPtr;
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
		Q_assert(rhiDesc.mipCount <= MaxTextureMips);

		bool requestTransition = false;

		// Alignment 0 is the same as specifying D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
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
		if(rhiDesc.allowedState & ResourceStates::UnorderedAccessBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		if(rhiDesc.allowedState & ResourceStates::RenderTargetBit)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		if(rhiDesc.allowedState & ResourceStates::DepthAccessBits)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}
		if((rhiDesc.allowedState & ResourceStates::ShaderAccessBits) == 0)
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

		D3D12_CLEAR_VALUE clearValue = {};
		const D3D12_CLEAR_VALUE* pClearValue = NULL;
		if(rhiDesc.usePreferredClearValue)
		{
			pClearValue = &clearValue;
			clearValue.Format = desc.Format;
			if(IsD3DDepthFormat(clearValue.Format))
			{
				clearValue.DepthStencil.Depth = rhiDesc.clearDepth;
				clearValue.DepthStencil.Stencil = rhiDesc.clearStencil;
			}
			else
			{
				memcpy(clearValue.Color, rhiDesc.clearColor, sizeof(clearValue.Color));
			}
		}

		// @TODO: initial state -> D3D12_RESOURCE_STATE
		D3D12MA::Allocation* allocation = NULL;
		ID3D12Resource* resource;
		if(rhiDesc.nativeResource != NULL)
		{
			resource = (ID3D12Resource*)rhiDesc.nativeResource;
		}
		else
		{
			D3D(rhi.allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COPY_DEST, pClearValue, &allocation, IID_PPV_ARGS(&resource)));
		}
		((TextureDesc&)rhiDesc).name = rhi.stringAllocator.Allocate(rhiDesc.name);
		SetDebugName(resource, rhiDesc.name, D3DResourceType::Texture);

		uint32_t srvIndex = InvalidDescriptorIndex;
		if(rhiDesc.allowedState & ResourceStates::ShaderAccessBits)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv = { 0 };
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Format = desc.Format;
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.Texture2D.MipLevels = desc.MipLevels;
			srv.Texture2D.MostDetailedMip = 0;
			srv.Texture2D.PlaneSlice = 0;
			srv.Texture2D.ResourceMinLODClamp = 0.0f;
			srvIndex = rhi.descHeapGeneric.CreateSRV(resource, srv);
		}

		uint32_t rtvIndex = InvalidDescriptorIndex;
		if(rhiDesc.allowedState & ResourceStates::RenderTargetBit)
		{
			D3D12_RENDER_TARGET_VIEW_DESC rtv = { 0 };
			rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtv.Format = desc.Format;
			rtv.Texture2D.MipSlice = 0;
			rtv.Texture2D.PlaneSlice = 0;
			rtvIndex = rhi.descHeapRTVs.CreateRTV(resource, rtv);
			requestTransition = true;
		}

		uint32_t dsvIndex = InvalidDescriptorIndex;
		if(rhiDesc.allowedState & ResourceStates::DepthWriteBit)
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv = { 0 };
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv.Format = desc.Format;
			dsv.Flags = D3D12_DSV_FLAG_NONE; // @TODO:
			dsv.Texture2D.MipSlice = 0;
			dsvIndex = rhi.descHeapDSVs.CreateDSV(resource, dsv);
			requestTransition = true;
		}

		Texture texture = {};
		texture.desc = rhiDesc;
		texture.allocation = allocation;
		texture.texture = resource;
		texture.srvIndex = srvIndex;
		texture.rtvIndex = rtvIndex;
		texture.dsvIndex = dsvIndex;
		texture.currentState = D3D12_RESOURCE_STATE_COPY_DEST;
		texture.shortLifeTime = rhiDesc.shortLifeTime;
		if(rhiDesc.allowedState & ResourceStates::UnorderedAccessBit)
		{
			for(uint32_t m = 0; m < rhiDesc.mipCount; ++m)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { 0 };
				uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				uav.Format = desc.Format;
				uav.Texture2D.MipSlice = m;
				uav.Texture2D.PlaneSlice = 0;
				texture.mips[m].uavIndex = rhi.descHeapGeneric.CreateUAV(resource, uav);
			}
		}
		else
		{
			for(uint32_t m = 0; m < rhiDesc.mipCount; ++m)
			{
				texture.mips[m].uavIndex = InvalidDescriptorIndex;
			}
		}

		const HTexture handle = rhi.textures.Add(texture);
		if(rhiDesc.nativeResource == NULL)
		{
			rhi.texturesToTransition.Add(handle);
		}

		return handle;
	}

	void DestroyTexture(HTexture handle)
	{
		Texture& texture = rhi.textures.Get(handle);
		if(texture.desc.allowedState & ResourceStates::ShaderAccessBits)
		{
			rhi.descHeapGeneric.Free(texture.srvIndex);
		}
		if(texture.desc.allowedState & ResourceStates::RenderTargetBit)
		{
			rhi.descHeapRTVs.Free(texture.rtvIndex);
		}
		if(texture.desc.allowedState & ResourceStates::DepthWriteBit)
		{
			rhi.descHeapDSVs.Free(texture.dsvIndex);
		}
		if(texture.desc.allowedState & ResourceStates::UnorderedAccessBit)
		{
			for(uint32_t m = 0; m < texture.desc.mipCount; ++m)
			{
				rhi.descHeapGeneric.Free(texture.mips[m].uavIndex);
			}
		}
		COM_RELEASE(texture.texture);
		COM_RELEASE(texture.allocation);
		rhi.textures.Remove(handle);
	}

	HSampler CreateSampler(const SamplerDesc& sampler)
	{
		const D3D12_TEXTURE_ADDRESS_MODE addressMode = GetD3DTextureAddressMode(sampler.wrapMode);
		D3D12_FILTER filter = GetD3DFilter(sampler.filterMode);
		UINT maxAnisotropy = r_ext_max_anisotropy->integer;
		if(filter == D3D12_FILTER_ANISOTROPIC && maxAnisotropy <= 1)
		{
			filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			maxAnisotropy = 1;
		}
		if(filter != D3D12_FILTER_ANISOTROPIC)
		{
			maxAnisotropy = 1;
		}

		D3D12_SAMPLER_DESC desc = { 0 };
		desc.AddressU = addressMode;
		desc.AddressV = addressMode;
		desc.AddressW = addressMode;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
		desc.MaxAnisotropy = maxAnisotropy;
		desc.MaxLOD = 0.0f;
		desc.Filter = filter;
		const uint32_t index = rhi.descHeapSamplers.CreateSampler(desc);

		return RHI_MAKE_HANDLE(CreateHandle(ResourceType::Sampler, index, 0));
	}

	void DestroySampler(HSampler sampler)
	{
		Handle type, index, gen;
		DecomposeHandle(&type, &index, &gen, sampler.v);
		Q_assert(type == ResourceType::Sampler);
		if(type != ResourceType::Sampler)
		{
			ri.Error(ERR_FATAL, "DestroySampler handle is not a sampler!\n");
			return;
		}

		rhi.descHeapSamplers.Free(index);
	}

	static void AddShaderVisibility(bool outVis[ShaderStage::Count], D3D12_SHADER_VISIBILITY inVis)
	{
		switch(inVis)
		{
			case D3D12_SHADER_VISIBILITY_VERTEX: outVis[ShaderStage::Vertex] = true; break;
			case D3D12_SHADER_VISIBILITY_PIXEL: outVis[ShaderStage::Pixel] = true; break;
			default: break;
		}
	}

	HRootSignature CreateRootSignature(const RootSignatureDesc& rhiDesc)
	{
		RootSignature rhiSignature = { 0 };
		rhiSignature.genericTableIndex = UINT32_MAX;
		rhiSignature.samplerTableIndex = UINT32_MAX;
		rhiSignature.genericDescCount = 0;
		rhiSignature.samplerDescCount = rhiDesc.samplerCount;

		bool shaderVis[ShaderStage::Count] = { 0 };

		//
		// root constants
		//
		int parameterCount = 0;
		D3D12_ROOT_PARAMETER parameters[16];
		for(int s = 0; s < ShaderStage::Count; ++s)
		{
			if(rhiDesc.constants[s].byteCount > 0)
			{
				rhiSignature.constants[s].parameterIndex = parameterCount;

				D3D12_ROOT_PARAMETER& p = parameters[parameterCount];
				p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
				p.Constants.Num32BitValues = AlignUp(rhiDesc.constants[s].byteCount, 4) / 4;
				p.Constants.RegisterSpace = 0;
				p.Constants.ShaderRegister = 0;
				p.ShaderVisibility = GetD3DVisibility((ShaderStage::Id)s);
				AddShaderVisibility(shaderVis, p.ShaderVisibility);

				parameterCount++;
			}
		}
		Q_assert(parameterCount <= ShaderStage::Count);

		//
		// CBV SRV UAV table
		//
		D3D12_DESCRIPTOR_RANGE genericRanges[ARRAY_LEN(rhiDesc.genericRanges)] = {};
		for(uint32_t rangeIndex = 0; rangeIndex < rhiDesc.genericRangeCount; ++rangeIndex)
		{
			D3D12_DESCRIPTOR_RANGE& r = genericRanges[rangeIndex];
			const RootSignatureDesc::DescriptorRange& rIn = rhiDesc.genericRanges[rangeIndex];
			Q_assert(rIn.count > 0);
			r.BaseShaderRegister = 0;
			r.NumDescriptors = rIn.count;
			r.OffsetInDescriptorsFromTableStart = rIn.firstIndex;
			r.RangeType = GetD3DDescriptorRangeType(rIn.type);
			r.RegisterSpace = 0;
			rhiSignature.genericDescCount += rIn.count;
		}
		if(rhiSignature.genericDescCount > 0)
		{
			rhiSignature.genericTableIndex = parameterCount;

			D3D12_ROOT_PARAMETER& p = parameters[parameterCount++];
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			p.DescriptorTable.NumDescriptorRanges = rhiDesc.genericRangeCount;
			p.DescriptorTable.pDescriptorRanges = genericRanges;
			p.ShaderVisibility = GetD3DVisibility(rhiDesc.genericVisibility);
			AddShaderVisibility(shaderVis, p.ShaderVisibility);
		}

		//
		// sampler table
		//
		D3D12_DESCRIPTOR_RANGE samplerRange = {};
		if(rhiDesc.samplerCount > 0)
		{
			rhiSignature.samplerTableIndex = parameterCount;

			D3D12_DESCRIPTOR_RANGE& r = samplerRange;
			r.BaseShaderRegister = 0;
			r.NumDescriptors = rhiDesc.samplerCount;
			r.OffsetInDescriptorsFromTableStart = 0;
			r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			r.RegisterSpace = 0;

			D3D12_ROOT_PARAMETER& p = parameters[parameterCount++];
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			p.DescriptorTable.NumDescriptorRanges = 1;
			p.DescriptorTable.pDescriptorRanges = &samplerRange;
			p.ShaderVisibility = GetD3DVisibility(rhiDesc.samplerVisibility);
			AddShaderVisibility(shaderVis, p.ShaderVisibility);
		}

		D3D12_ROOT_SIGNATURE_DESC desc = { 0 };
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
		if(!shaderVis[ShaderStage::Vertex])
		{
			desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
		}
		if(!shaderVis[ShaderStage::Pixel])
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
		((RootSignatureDesc&)rhiDesc).name = rhi.stringAllocator.Allocate(rhiDesc.name);
		SetDebugName(signature, rhiDesc.name, D3DResourceType::RootSignature);

		rhiSignature.desc = rhiDesc;
		rhiSignature.signature = signature;
		rhiSignature.shortLifeTime = rhiDesc.shortLifeTime;

		return rhi.rootSignatures.Add(rhiSignature);
	}

	void DestroyRootSignature(HRootSignature signature)
	{
		COM_RELEASE(rhi.rootSignatures.Get(signature).signature);
		rhi.rootSignatures.Remove(signature);
	}

	HDescriptorTable CreateDescriptorTable(const DescriptorTableDesc& desc)
	{
		const RootSignature& sig = rhi.rootSignatures.Get(desc.rootSignature);

		const char* srvName = rhi.stringAllocator.Allocate(va("%s GPU-visible CBV SRV UAV", desc.name));
		const char* samName = rhi.stringAllocator.Allocate(va("%s GPU-visible sampler", desc.name));

		DescriptorTable table = { 0 };
		table.genericHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, sig.genericDescCount, true, srvName);
		table.samplerHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, sig.samplerDescCount, true, samName);
		table.shortLifeTime = desc.shortLifeTime;

		const Texture& nullTex = rhi.textures.Get(rhi.nullTexture);
		const Texture& nullRWTex = rhi.textures.Get(rhi.nullRWTexture);
		//const Buffer& nullBuffer = rhi.buffers.Get(rhi.nullBuffer);
		//const Buffer& nullRWBuffer = rhi.buffers.Get(rhi.nullRWBuffer);

		// bind null CBV SRV UAV resources
		for(uint32_t r = 0; r < sig.desc.genericRangeCount; ++r)
		{
			const RootSignatureDesc::DescriptorRange& range = sig.desc.genericRanges[r];

			uint32_t index;
			switch(range.type)
			{
				case DescriptorType::Texture: index = nullTex.srvIndex; break;
				case DescriptorType::RWTexture: index = nullRWTex.mips[0].uavIndex; break;
				//case DescriptorType::Buffer: index = nullBuffer.XXX; break;
				//case DescriptorType::RWBuffer: index = nullRWBuffer.XXX; break;
				default: Q_assert(!"Unsupported descriptor type"); continue;
			}

			for(uint32_t i = 0; i < range.count; ++i)
			{
				CopyDescriptor(table.genericHeap, range.firstIndex + i, rhi.descHeapGeneric, index);
			}
		}

		// bind null samplers
		for(uint32_t d = 0; d < sig.desc.samplerCount; ++d)
		{
			Handle type, index, gen;
			DecomposeHandle(&type, &index, &gen, rhi.nullSampler.v);
			CopyDescriptor(table.samplerHeap, d, rhi.descHeapSamplers, index);
		}

		return rhi.descriptorTables.Add(table);
	}

	void UpdateDescriptorTable(HDescriptorTable htable, const DescriptorTableUpdate& update)
	{
		Q_assert(update.textures != NULL);

		DescriptorTable& table = rhi.descriptorTables.Get(htable);
		
		if(update.type == DescriptorType::Texture && table.genericHeap)
		{
			for(uint32_t i = 0; i < update.resourceCount; ++i)
			{
				const Texture& texture = rhi.textures.Get(update.textures[i]);
				CopyDescriptor(table.genericHeap, update.firstIndex + i, rhi.descHeapGeneric, texture.srvIndex);
			}
		}
		else if(update.type == DescriptorType::RWTexture && table.genericHeap)
		{
			uint32_t destIndex = update.firstIndex;
			for(uint32_t i = 0; i < update.resourceCount; ++i)
			{
				const Texture& texture = rhi.textures.Get(update.textures[i]);
				uint32_t start;
				uint32_t end;
				if(update.uavMipChain)
				{
					start = 0;
					end = texture.desc.mipCount;
				}
				else
				{
					Q_assert(update.uavMipSlice < texture.desc.mipCount);
					start = update.uavMipSlice;
					end = start + 1;
				}

				for(uint32_t m = start; m < end; ++m)
				{
					CopyDescriptor(table.genericHeap, destIndex++, rhi.descHeapGeneric, texture.mips[m].uavIndex);
				}
			}
		}
		else if(update.type == DescriptorType::Sampler && table.samplerHeap)
		{
			for(uint32_t i = 0; i < update.resourceCount; ++i)
			{
				Handle htype, index, gen;
				DecomposeHandle(&htype, &index, &gen, update.samplers[i].v);
				CopyDescriptor(table.samplerHeap, update.firstIndex + i, rhi.descHeapSamplers, index);
			}
		}
		else
		{
			ri.Error(ERR_FATAL, "UpdateDescriptorTable: unsupported descriptor type\n");
		}
	}

	void DestroyDescriptorTable(HDescriptorTable handle)
	{
		DescriptorTable& table = rhi.descriptorTables.Get(handle);
		COM_RELEASE(table.genericHeap);
		COM_RELEASE(table.samplerHeap);

		rhi.descriptorTables.Remove(handle);
	}

	HPipeline CreateGraphicsPipeline(const GraphicsPipelineDesc& rhiDesc)
	{
		Q_assert(rhi.rootSignatures.Get(rhiDesc.rootSignature).desc.pipelineType == PipelineType::Graphics);

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
		((GraphicsPipelineDesc&)rhiDesc).name = rhi.stringAllocator.Allocate(rhiDesc.name);
		SetDebugName(pso, rhiDesc.name, D3DResourceType::PipelineState);

		Pipeline rhiPipeline;
		rhiPipeline.type = PipelineType::Graphics;
		rhiPipeline.graphicsDesc = rhiDesc;
		rhiPipeline.pso = pso;
		rhiPipeline.shortLifeTime = rhiDesc.shortLifeTime;

		return rhi.pipelines.Add(rhiPipeline);
	}

	HPipeline CreateComputePipeline(const ComputePipelineDesc& rhiDesc)
	{
		Q_assert(rhi.rootSignatures.Get(rhiDesc.rootSignature).desc.pipelineType == PipelineType::Compute);

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = { 0 };
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE; // none available so far
		desc.pRootSignature = rhi.rootSignatures.Get(rhiDesc.rootSignature).signature;
		desc.CS.pShaderBytecode = rhiDesc.shader.data;
		desc.CS.BytecodeLength = rhiDesc.shader.byteCount;

		ID3D12PipelineState* pso;
		D3D(rhi.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso)));
		((ComputePipelineDesc&)rhiDesc).name = rhi.stringAllocator.Allocate(rhiDesc.name);
		SetDebugName(pso, rhiDesc.name, D3DResourceType::PipelineState);

		Pipeline rhiPipeline;
		rhiPipeline.type = PipelineType::Compute;
		rhiPipeline.computeDesc = rhiDesc;
		rhiPipeline.pso = pso;
		rhiPipeline.shortLifeTime = rhiDesc.shortLifeTime;

		return rhi.pipelines.Add(rhiPipeline);
	}

	void DestroyPipeline(HPipeline pipeline)
	{
		COM_RELEASE(rhi.pipelines.Get(pipeline).pso);
		rhi.pipelines.Remove(pipeline);
	}

	void CmdBindRenderTargets(uint32_t colorCount, const HTexture* colorTargets, const HTexture* depthStencilTarget)
	{
		Q_assert(CanWriteCommands());
		Q_assert(colorCount > 0 || colorTargets == NULL);

		const uint32_t rtvIndex = rhi.textures.Get(rhi.renderTargets[rhi.frameIndex]).rtvIndex;
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rhi.descHeapRTVs.GetCPUHandle(rtvIndex);

		Q_assert(depthStencilTarget != NULL);
		const Texture& depthStencil = rhi.textures.Get(*depthStencilTarget);
		const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = rhi.descHeapDSVs.GetCPUHandle(depthStencil.dsvIndex);

		rhi.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);
		rhi.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	}

	void CmdBindRootSignature(HRootSignature rootSignature)
	{
		Q_assert(CanWriteCommands());

		const RootSignature& sig = rhi.rootSignatures.Get(rootSignature);
		if(sig.desc.pipelineType == PipelineType::Graphics && rootSignature != rhi.currentRootSignature)
		{
			rhi.currentRootSignature = rootSignature;
			rhi.commandList->SetGraphicsRootSignature(sig.signature);
		}
		else if(sig.desc.pipelineType == PipelineType::Compute)
		{
			rhi.commandList->SetComputeRootSignature(sig.signature);
		}
	}

	void CmdBindDescriptorTable(HRootSignature sigHandle, HDescriptorTable handle)
	{
		Q_assert(CanWriteCommands());

		const DescriptorTable& table = rhi.descriptorTables.Get(handle);
		const RootSignature& sig = rhi.rootSignatures.Get(sigHandle);

		UINT heapCount = 0;
		ID3D12DescriptorHeap* heaps[2];
		if(sig.genericTableIndex != UINT32_MAX)
		{
			heaps[heapCount++] = table.genericHeap;
		}
		if(sig.samplerTableIndex != UINT32_MAX)
		{
			heaps[heapCount++] = table.samplerHeap;
		}
		rhi.commandList->SetDescriptorHeaps(heapCount, heaps);

		if(sig.genericTableIndex != UINT32_MAX)
		{
			if(sig.desc.pipelineType == PipelineType::Graphics)
			{
				rhi.commandList->SetGraphicsRootDescriptorTable(sig.genericTableIndex, table.genericHeap->GetGPUDescriptorHandleForHeapStart());
			}
			else if(sig.desc.pipelineType == PipelineType::Compute)
			{
				rhi.commandList->SetComputeRootDescriptorTable(sig.genericTableIndex, table.genericHeap->GetGPUDescriptorHandleForHeapStart());
			}
		}
		if(sig.samplerTableIndex != UINT32_MAX)
		{
			if(sig.desc.pipelineType == PipelineType::Graphics)
			{
				rhi.commandList->SetGraphicsRootDescriptorTable(sig.samplerTableIndex, table.samplerHeap->GetGPUDescriptorHandleForHeapStart());
			}
			else if(sig.desc.pipelineType == PipelineType::Compute)
			{
				rhi.commandList->SetComputeRootDescriptorTable(sig.samplerTableIndex, table.samplerHeap->GetGPUDescriptorHandleForHeapStart());
			}
		}
	}

	void CmdBindPipeline(HPipeline pipeline)
	{
		Q_assert(CanWriteCommands());

		const Pipeline& pipe = rhi.pipelines.Get(pipeline);
		if(pipe.type == PipelineType::Graphics)
		{
			// @TODO: grab from pipe + cache
			rhi.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		}
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

	void CmdSetRootConstants(HRootSignature rootSignature, ShaderStage::Id shaderType, const void* constants)
	{
		Q_assert(CanWriteCommands());
		Q_assert(constants);

		const RootSignature& sig = rhi.rootSignatures.Get(rootSignature);
		const UINT parameterIndex = sig.constants[shaderType].parameterIndex;
		const UINT constantCount = sig.desc.constants[shaderType].byteCount / 4;

		CmdBindRootSignature(rootSignature);

		if(sig.desc.pipelineType == PipelineType::Graphics)
		{
			rhi.commandList->SetGraphicsRoot32BitConstants(parameterIndex, constantCount, constants, 0);
		}
		else if(sig.desc.pipelineType == PipelineType::Compute)
		{
			rhi.commandList->SetComputeRoot32BitConstants(parameterIndex, constantCount, constants, 0);
		}
	}

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

	void CmdDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
	{
		Q_assert(CanWriteCommands());

		rhi.commandList->Dispatch(groupCountX, groupCountY, groupCountZ);
	}

	HDurationQuery CmdBeginDurationQuery(const char* name)
	{
		Q_assert(CanWriteCommands());

		FrameQueries& fq = rhi.frameQueries[rhi.frameIndex];
		Q_assert(fq.durationQueryCount < MaxDurationQueries);
		if(fq.durationQueryCount >= MaxDurationQueries)
		{
			return RHI_MAKE_NULL_HANDLE();
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

		return RHI_MAKE_HANDLE(query.handle);
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

	void CmdBarrier(uint32_t texCount, const TextureBarrier* textures, uint32_t buffCount, const BufferBarrier* buffers)
	{
		Q_assert(CanWriteCommands());

		static D3D12_RESOURCE_BARRIER barriers[MAX_DRAWIMAGES * 2];
		Q_assert(buffCount + texCount <= ARRAY_LEN(barriers));

		UINT barrierCount = 0;
		for(uint32_t i = 0; i < texCount; ++i)
		{
			Texture& texture = rhi.textures.Get(textures[i].texture);
			if(SetBarrier(texture.currentState, barriers[barrierCount], textures[i].newState, texture.texture))
			{
				barrierCount++;
			}
			
		}
		for(uint32_t i = 0; i < buffCount; ++i)
		{
			Buffer& buffer = rhi.buffers.Get(buffers[i].buffer);
			if(SetBarrier(buffer.currentState, barriers[barrierCount], buffers[i].newState, buffer.buffer))
			{
				barrierCount++;
			}
		}

		if(barrierCount > 0)
		{
			rhi.commandList->ResourceBarrier(barrierCount, barriers);
		}
	}

	uint8_t* BeginBufferUpload(HBuffer buffer)
	{
		return rhi.upload.BeginBufferUpload(buffer);
	}

	void EndBufferUpload(HBuffer buffer)
	{
		rhi.upload.EndBufferUpload(buffer);
	}

	void BeginTextureUpload(MappedTexture& mappedTexture, HTexture texture)
	{
		rhi.upload.BeginTextureUpload(mappedTexture, texture);
	}

	void EndTextureUpload(HTexture texture)
	{
		rhi.upload.EndTextureUpload(texture);
	}

	void BeginTempCommandList()
	{
		Q_assert(rhi.commandList == rhi.mainCommandList);
		rhi.commandList = rhi.tempCommandList;

		// CPU wait for the temp command list to be done executing on the GPU
		WaitForTempCommandList();

		// GPU wait for the copy queue to be done executing on the GPU
		rhi.upload.WaitToStartDrawing(rhi.computeCommandQueue);
	}

	void EndTempCommandList()
	{
		Q_assert(rhi.commandList == rhi.tempCommandList);
		rhi.commandList = rhi.mainCommandList;

		// execute and wait on the temporary command list
		ID3D12CommandQueue* const queue = rhi.computeCommandQueue;
		rhi.tempCommandList->Close();
		ID3D12CommandList* tempCommandListArray[] = { rhi.tempCommandList };
		queue->ExecuteCommandLists(ARRAY_LEN(tempCommandListArray), tempCommandListArray);
		rhi.tempFenceValue++;
		rhi.tempFence.Signal(queue, rhi.tempFenceValue);
		rhi.tempCommandListOpen = false;
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

	ShaderByteCode CompileComputeShader(const char* source)
	{
		return CompileShader(source, "cs_5_1");
	}

#endif
}

void R_GUI_RHI()
{
	RHI::DrawGUI();
}
