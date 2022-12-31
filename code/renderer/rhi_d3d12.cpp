/*
===========================================================================
Copyright (C) 2022 Gian 'myT' Schellenbaum

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

* partial inits and shutdown
- move the Dear ImGui rendering outside of the RHI
* integrate D3D12MA
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


#include "tr_local.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#if defined(_DEBUG)
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")
#endif
#include "D3D12MemAlloc.h"
// @TODO: move out of the RHI...
#include "../imgui/imgui_impl_dx12.h"


// @TODO: Q3 macro to specify D3D12SDKVersion
// OR... include our own Agility SDK and specify D3D12_SDK_VERSION
#if defined(_DEBUG)
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 608; }
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
			RootSignature,
			Pipeline,
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

	struct RootSignature
	{
		ID3D12RootSignature* signature;
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
		ID3D12DescriptorHeap* srvHeap; // SRV + UAV + CBV
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		UINT rtvIncSize;
		ID3D12Resource* renderTargets[FrameCount];
		ID3D12CommandAllocator* commandAllocators[FrameCount];
		ID3D12GraphicsCommandList* commandList;
		UINT frameIndex;
		HANDLE fenceEvent;
		ID3D12Fence* fence;
		UINT64 fenceValues[FrameCount];

#define POOL(Type, Size) StaticPool<Type, H##Type, ResourceType::Type, Size>
		POOL(Fence, 64) fences;
		POOL(Buffer, 64) buffers;
		POOL(RootSignature, 64) rootSignatures;
		POOL(Pipeline, 64) pipelines;
#undef POOL
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
		// ID3D12Object::SetName is a Unicode wrapper for
		// ID3D12Object::SetPrivateData with WKPDID_D3DDebugObjectNameW
		resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(resourceName), resourceName);
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

	void Init()
	{
		Sys_V_Init();

		if(rhi.device != NULL)
		{
			return;
		}

		memset(&rhi, 0, sizeof(rhi));

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
			heapDesc.NumDescriptors = MAX_DRAWIMAGES * 2;
			heapDesc.NodeMask = 0;
			D3D(rhi.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rhi.srvHeap)));
			SetDebugName(rhi.srvHeap, "CBV SRV UAV Descriptor Heap");
		}

		// queue some actual work...

		D3D(rhi.commandList->Close());

		WaitUntilDeviceIsIdle();

		glInfo.maxTextureSize = 2048;
		glInfo.maxAnisotropy = 16;
		glInfo.depthFadeSupport = qfalse;

		if(!ImGui_ImplDX12_Init(rhi.device, FrameCount, DXGI_FORMAT_R8G8B8A8_UNORM, rhi.srvHeap,
			rhi.srvHeap->GetCPUDescriptorHandleForHeapStart(),
			rhi.srvHeap->GetGPUDescriptorHandleForHeapStart()))
		{
			ri.Error(ERR_FATAL, "Failed to initialize graphics objects for Dear ImGUI\n");
		}
	}

	void ShutDown(qbool destroyWindow)
	{
		if(!destroyWindow)
		{
			WaitUntilDeviceIsIdle();
			return;
		}

		ImGui_ImplDX12_Shutdown();

		Handle handle;
#define DESTROY_POOL(PoolName, FuncName) \
		for(int i = 0; rhi.PoolName.FindNext(&handle, &i);) \
			FuncName(MAKE_HANDLE(handle)); \
		rhi.PoolName.Clear()
		DESTROY_POOL(fences, DestroyFence);
		DESTROY_POOL(buffers, DestroyBuffer);
		DESTROY_POOL(rootSignatures, DestroyRootSignature);
		DESTROY_POOL(pipelines, DestroyPipeline);
#undef DESTROY_POOL

		WaitUntilDeviceIsIdle();

		CloseHandle(rhi.fenceEvent);

		COM_RELEASE(rhi.fence);
		COM_RELEASE(rhi.commandList);
		COM_RELEASE_ARRAY(rhi.commandAllocators);
		COM_RELEASE_ARRAY(rhi.renderTargets);
		COM_RELEASE(rhi.srvHeap);
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
			OutputDebugStringA("CNQ3: calling ReportLiveObjects\n");
			const HRESULT hr = debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			OutputDebugStringA(va("CNQ3: ReportLiveObjects returned 0x%08X (%s)\n", (unsigned int)hr, GetSystemErrorString(hr)));
			debug->Release();
		}
#endif
	}

	void BeginFrame()
	{
		// reclaim used memory
		D3D(rhi.commandAllocators[rhi.frameIndex]->Reset());

		// start recording
		D3D(rhi.commandList->Reset(rhi.commandAllocators[rhi.frameIndex], NULL));

		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = rhi.renderTargets[rhi.frameIndex];
		barrier.Transition.Subresource = 0; // D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rhi.commandList->ResourceBarrier(1, &barrier);

		rhi.commandList->SetDescriptorHeaps(1, &rhi.srvHeap);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { 0 };
		rtvHandle.ptr = rhi.rtvHandle.ptr + rhi.frameIndex * rhi.rtvIncSize;
		rhi.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
		const FLOAT clearColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		rhi.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
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
			ImGui::ShowDemoWindow();
			ImGui::EndFrame();
			ImGui::Render();
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

		// stop recording
		D3D(rhi.commandList->Close());

		ID3D12CommandList* commandListArray[] = { rhi.commandList };
		rhi.commandQueue->ExecuteCommandLists(ARRAY_LEN(commandListArray), commandListArray);

		Present();

		MoveToNextFrame();
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
		// @TODO:

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

		D3D12MA::ALLOCATION_DESC allocDesc = { 0 };
		allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
		allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_STRATEGY_MIN_MEMORY;
		// add D3D12MA::ALLOCATION_FLAG_COMMITTED for big resources

		D3D12MA::Allocation* allocation;
		ID3D12Resource* resource;
		D3D(rhi.allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &allocation, IID_PPV_ARGS(&resource)));
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

	HRootSignature CreateRootSignature(const RootSignatureDesc& rhiDesc)
	{
		// @TODO: flags for vertex and pixel shader access etc.
		ID3DBlob* blob;
		//ID3DBlob* errorBlob; // @TODO:
		D3D12_ROOT_SIGNATURE_DESC desc = { 0 };
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
		desc.NumParameters = 0;
		desc.pParameters = NULL;
		desc.NumStaticSamplers = 0;
		desc.pStaticSamplers = NULL;
		D3D(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL));

		ID3D12RootSignature* signature;
		D3D(rhi.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature)));
		COM_RELEASE(blob);

		RootSignature rhiSignature = { 0 };
		rhiSignature.signature = signature;

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

		desc.InputLayout.NumElements = 0;
		desc.InputLayout.pInputElementDescs = NULL;

		desc.NumRenderTargets = 1;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // RGBA
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		desc.DepthStencilState.DepthEnable = FALSE; // toggles depth *testing*
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // D3D12_DEPTH_WRITE_MASK_ALL to enable writes
		desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		
		desc.VS.pShaderBytecode = rhiDesc.vertexShader.data;
		desc.VS.BytecodeLength = rhiDesc.vertexShader.byteCount;
		desc.PS.pShaderBytecode = rhiDesc.pixelShader.data;
		desc.PS.BytecodeLength = rhiDesc.pixelShader.byteCount;

		desc.RasterizerState.AntialiasedLineEnable = FALSE;
		desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.FrontCounterClockwise = FALSE;
		desc.RasterizerState.DepthBias = 0;
		desc.RasterizerState.DepthBiasClamp = 0.0f;
		desc.RasterizerState.SlopeScaledDepthBias = 0.0f;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.ForcedSampleCount = 0;
		desc.RasterizerState.MultisampleEnable = FALSE;

		ID3D12PipelineState* pso;
		D3D(rhi.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));

		Pipeline rhiPipeline = { 0 };
		rhiPipeline.type = PipelineType::Graphics;
		rhiPipeline.graphicsDesc = rhiDesc;
		rhiPipeline.pso = pso;

		return rhi.pipelines.Add(rhiPipeline);
	}

	void DestroyPipeline(HPipeline pipeline)
	{
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

		count = max(count, MaxVertexBufferCount);

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

	void CmdDraw(uint32_t vertexCount, uint32_t firstVertex)
	{
		Q_assert(CanWriteCommands());

		rhi.commandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
	}

#if defined(_DEBUG)

	static ShaderByteCode CompileShader(const char* source, const char* target)
	{
		// yup, this leaks memory but we don't care as it's for quick and dirty testing
		// could write to a linear allocator instead...
		ID3DBlob* blob;
		ID3DBlob* error;
		const UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
		if(FAILED(D3DCompile(source, strlen(source), NULL, NULL, NULL, "main", target, flags, 0, &blob, &error)))
		{
			ri.Error(ERR_FATAL, "Shader compilation failed:\n%s\n", (const char*)error->GetBufferPointer());
			return ShaderByteCode();
		}

		ShaderByteCode byteCode;
		byteCode.data = blob->GetBufferPointer();
		byteCode.byteCount = blob->GetBufferSize();

		return byteCode;
	}

	ShaderByteCode CompileVertexShader(const char* source)
	{
		return CompileShader(source, "vs_5_0");
	}

	ShaderByteCode CompilePixelShader(const char* source)
	{
		return CompileShader(source, "ps_5_0");
	}

#endif
}
