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


#include "tr_local.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>


#if defined(_DEBUG) // @TODO: Q3 macro to specify D3D12SDKVersion
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 608; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }
#endif


const UINT FrameCount = 2;

struct RHIPrivate
{
	ID3D12Debug* debug; // can be NULL
	IDXGIFactory1* dxgiFactory1;
	ID3D12Device* device;
	ID3D12CommandQueue* commandQueue;
	IDXGISwapChain3* dxgiSwapChain3;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	UINT rtvIncSize;
	ID3D12Resource* renderTargets[FrameCount];
	ID3D12CommandAllocator* commandAllocator;
	ID3D12GraphicsCommandList* commandList;
	UINT frameIndex;
	HANDLE fenceEvent;
	ID3D12Fence* fence;
	UINT64 fenceValue;
};

static RHIPrivate rhi;


#define COM_RELEASE(p)       do { if(p) { p->Release(); p = NULL; } } while((void)0,0)
#define COM_RELEASE_ARRAY(a) do { for(int i = 0; i < ARRAY_LEN(a); ++i) { COM_RELEASE(a[i]); } } while((void)0,0)

#define D3D(Exp)             Check((Exp), #Exp)


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

static qbool CheckAndName(HRESULT hr, const char* function, ID3D12DeviceChild* resource, const char* resourceName)
{
	if(SUCCEEDED(hr))
	{
		resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(resourceName), resourceName);
		return qtrue;
	}

	if(1) // @TODO: fatal error mode always on for now
	{
		ri.Error(ERR_FATAL, "'%s' failed to create '%s' with code 0x%08X (%s)\n", function, resourceName, (unsigned int)hr, GetSystemErrorString(hr));
	}
	return qfalse;
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
	static void WaitForPreviousFrame()
	{
		// @TODO: better approach

		// signal and increment the fence value
		const UINT64 fence = rhi.fenceValue;
		D3D(rhi.commandQueue->Signal(rhi.fence, fence));
		rhi.fenceValue++;

		// wait until the previous frame is finished
		if(rhi.fence->GetCompletedValue() < fence)
		{
			D3D(rhi.fence->SetEventOnCompletion(fence, rhi.fenceEvent));
			WaitForSingleObject(rhi.fenceEvent, INFINITE);
		}

		rhi.frameIndex = rhi.dxgiSwapChain3->GetCurrentBackBufferIndex();
	}

	void Init()
	{
		Sys_V_Init();

		HRESULT hr = S_OK;

#if defined(_DEBUG)
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&rhi.debug))))
		{
			// calling after device creation will remove the device
			// if you hit this error:
			// D3D Error 887e0003: (13368@153399640) at 00007FFC84ECF985 - D3D12 SDKLayers dll does not match the D3D12SDKVersion of D3D12 Core dll.
			// make sure your D3D12SDKVersion and D3D12SDKPath are valid!
			rhi.debug->EnableDebugLayer();
		}
#endif

		hr = CreateDXGIFactory1(IID_PPV_ARGS(&rhi.dxgiFactory1));
		Check(hr, "CreateDXGIFactory1");
		IDXGIAdapter1* adapter;
		UINT i = 0;
		while(rhi.dxgiFactory1->EnumAdapters1(i++, &adapter) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			//desc.Description;
			//__debugbreak();
			// can check if device supports D3D12:
			//if(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), NULL)))
		}

		// @TODO: first argument is the adapter, NULL is default
		hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&rhi.device));
		Check(hr, "D3D12CreateDevice");

		D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { 0 };
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		commandQueueDesc.NodeMask = 0;
		hr = rhi.device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&rhi.commandQueue));
		Check(hr, "ID3D12Device::CreateCommandQueue");

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
		hr = rhi.dxgiFactory1->CreateSwapChain(rhi.commandQueue, &swapChainDesc, &dxgiSwapChain);
		Check(hr, "IDXGIFactory::CreateSwapChain");

		hr = dxgiSwapChain->QueryInterface(IID_PPV_ARGS(&rhi.dxgiSwapChain3));
		Check(hr, "IDXGISwapChain::QueryInterface");
		rhi.frameIndex = rhi.dxgiSwapChain3->GetCurrentBackBufferIndex();
		COM_RELEASE(dxgiSwapChain);

		ID3D12DescriptorHeap* rtvHeap;
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = { 0 };
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.NodeMask = 0;
		hr = rhi.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
		Check(hr, "ID3D12Device::CreateDescriptorHeap");

		rhi.rtvIncSize = rhi.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		rhi.rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleIt = rhi.rtvHandle;
		for(UINT f = 0; f < FrameCount; ++f)
		{
			hr = rhi.dxgiSwapChain3->GetBuffer(f, IID_PPV_ARGS(&rhi.renderTargets[f]));
			Check(hr, "IDXGIFactory::GetBuffer");
			rhi.device->CreateRenderTargetView(rhi.renderTargets[f], NULL, rtvHandleIt);
			rtvHandleIt.ptr += rhi.rtvIncSize;
		}

		hr = rhi.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rhi.commandAllocator));
		Check(hr, "ID3D12Device::CreateCommandAllocator");

		hr = rhi.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rhi.commandAllocator, NULL, IID_PPV_ARGS(&rhi.commandList));
		Check(hr, "ID3D12Device::CreateCommandList");
		hr = rhi.commandList->Close();
		Check(hr, "ID3D12GraphicsCommandList::Close");

		hr = rhi.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&rhi.fence));
		Check(hr, "ID3D12Device::CreateFence");
		rhi.fenceValue = 1;

		rhi.fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if(rhi.fenceEvent == NULL)
		{
			Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
		}

		// queue work...

		WaitForPreviousFrame();

		glInfo.maxTextureSize = 2048;
		glInfo.maxAnisotropy = 0;
		glInfo.depthFadeSupport = qfalse;
		glInfo.mipGenSupport = qtrue;
		glInfo.alphaToCoverageSupport = qfalse;
		glInfo.msaaSampleCount = 1;
	}

	void ShutDown()
	{
		// @TODO: use the debug interface from DXGIGetDebugInterface to enumerate what's alive

		WaitForPreviousFrame();

		CloseHandle(rhi.fenceEvent);

		// @TODO: release all the COM resources...
	}

	void BeginFrame()
	{
		D3D(rhi.commandAllocator->Reset());
		D3D(rhi.commandList->Reset(rhi.commandAllocator, NULL));

		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = rhi.renderTargets[rhi.frameIndex];
		barrier.Transition.Subresource = 0; // D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		rhi.commandList->ResourceBarrier(1, &barrier);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = { 0 };
		rtvHandle.ptr = rhi.rtvHandle.ptr + rhi.frameIndex * rhi.rtvIncSize;
		rhi.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
		const FLOAT clearColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		rhi.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
	}

	void EndFrame()
	{
		D3D12_RESOURCE_BARRIER barrier = { 0 };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = rhi.renderTargets[rhi.frameIndex];
		barrier.Transition.Subresource = 0; // D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		rhi.commandList->ResourceBarrier(1, &barrier);

		D3D(rhi.commandList->Close());

		ID3D12CommandList* commandListArray[] = { rhi.commandList };
		rhi.commandQueue->ExecuteCommandLists(ARRAY_LEN(commandListArray), commandListArray);

		// DXGI_PRESENT_ALLOW_TEARING
		D3D(rhi.dxgiSwapChain3->Present(r_swapInterval->integer, 0));

		WaitForPreviousFrame();
	}
}
