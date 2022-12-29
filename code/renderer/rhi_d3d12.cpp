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
- partial inits and shutdown
- move the Dear ImGui rendering outside of the RHI
- integrate D3D12MA
- compiler switch for GPU validation
- D3DCOMPILE_DEBUG for shaders
- use ID3D12Device4::CreateCommandList1 to create closed command lists
- if a feature level below 12.0 is good enough,
	use ID3D12Device::CheckFeatureSupport with D3D12_FEATURE_D3D12_OPTIONS / D3D12_FEATURE_DATA_D3D12_OPTIONS
	to ensure Resource Binding Tier 2 is available
- IDXGISwapChain::SetFullScreenState(TRUE) with the borderless window taking up the entire screen
	and ALLOW_TEARING set on both the flip mode swap chain and Present() flags
	will enable true immediate independent flip mode and give us the lowest latency possible
- NvAPI_D3D_GetLatency to get (simulated) input to display latency
- NvAPI_D3D_IsGSyncCapable / NvAPI_D3D_IsGSyncActive for diagnostics
*/


#include "tr_local.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
// @TODO: move out of the RHI...
#include "../imgui/imgui_impl_dx12.h"


#if defined(_DEBUG) // @TODO: Q3 macro to specify D3D12SDKVersion
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 608; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }
#endif


// this has 2 meanings:
// 1. maximum number of frames queued
// 2. number of frames in the back buffer
static const UINT FrameCount = 2;

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
	ID3D12Device* device;
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

static qbool Check(HRESULT hr, const char* function)
{
	if(SUCCEEDED(hr))
	{
		return qtrue;
	}

	if(1) // @TODO: fatal error mode always on for now
	{
		ri.Error(ERR_FATAL, "'%s' failed with code 0x%08X (%s)\n", function, (unsigned int)hr, GetSystemErrorString(hr));
	}
	return qfalse;
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


namespace RHI
{
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

		// D3D_FEATURE_LEVEL_12_0 is the minimum to ensure at least Resource Binding Tier 2:
		// - unlimited SRVs
		// - 14 CBVs
		// - 64 UAVs
		// - 2048 samplers
		const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

		// @TODO: enumerate adapters with the right feature levels and pick the right one
		/*
		You could also query a IDXGIFactory6 interface and use EnumAdapterByGpuPreference to prefer using discrete
		(DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE) vs. integrated (DXGI_GPU_PREFERENCE_MINIMUM_POWER) graphics on hybrid systems.
		This interface is supported on Windows 10 April 2018 Update (17134) or later.
		*/
#if 0
		{
			IDXGIAdapter1* adapter;
			UINT i = 0;
			while(rhi.factory->EnumAdapters1(i++, &adapter) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_ADAPTER_DESC1 desc;
				if(FAILED(adapter->GetDesc1(&desc)))
				{
					continue;
				}
				if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					continue;
				}
				if(FAILED(D3D12CreateDevice(adapter, featureLevel, __uuidof(ID3D12Device), NULL)))
				{
					continue;
				}
				// we have a valid candidate, do something
				//desc.Description;
				//__debugbreak();
			}
		}
#endif

		// @TODO: first argument is the adapter, NULL is default
		D3D(D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&rhi.device)));

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
		glInfo.mipGenSupport = qtrue;
		glInfo.alphaToCoverageSupport = qfalse;
		glInfo.msaaSampleCount = 1;

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
		COM_RELEASE(rhi.device);
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
}
