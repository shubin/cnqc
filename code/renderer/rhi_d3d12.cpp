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
#include <dxgi.h>


struct RHIPrivate
{
	ID3D12Debug* debug; // can be NULL
	IDXGIFactory1* dxgiFactory1;
	ID3D12Device* device;
	IDXGISwapChain* dxgiSwapChain;

};

static RHIPrivate rhi;


#define COM_RELEASE(p)       do { if(p) { p->Release(); p = NULL; } } while((void)0,0)
#define COM_RELEASE_ARRAY(a) do { for(int i = 0; i < ARRAY_LEN(a); ++i) { COM_RELEASE(a[i]); } } while((void)0,0)


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
	void Init()
	{
		Sys_V_Init();

		HRESULT hr = S_OK;

#if defined(_DEBUG)
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&rhi.debug))))
		{
			// @NOTE: calling after device creation will remove the device
			// @TODO: hitting this error :(
			// D3D Error 887e0003: (13368@153399640) at 00007FFC84ECF985 - D3D12 SDKLayers dll does not match the D3D12SDKVersion of D3D12 Core dll.
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

		DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
		/*swapChainDesc.BufferCount = ;
		swapChainDesc.BufferDesc.Format = ;
		swapChainDesc.BufferDesc.Width = ;
		swapChainDesc.BufferDesc.Height = ;
		swapChainDesc.BufferDesc.RefreshRate = ;
		swapChainDesc.BufferDesc.Scaling = ;
		swapChainDesc.BufferDesc.ScanlineOrdering = ;
		swapChainDesc.BufferUsage = ;
		swapChainDesc.Flags = ;
		swapChainDesc.OutputWindow = ;
		swapChainDesc.SampleDesc.Count = ;
		swapChainDesc.SampleDesc.Quality = ;
		swapChainDesc.SwapEffect = ;
		swapChainDesc.Windowed = ;*/
		hr = rhi.dxgiFactory1->CreateSwapChain(rhi.device, &swapChainDesc, &rhi.dxgiSwapChain);
		Check(hr, "IDXGIFactory::CreateSwapChain");
		//rhi.dxgiSwapChain->QueryInterface(IID_PPV_ARGS(&rhi.));
		//IDXGISwapChain3::GetCurrentBackBufferIndex
	}

	void ShutDown()
	{
		// use DXGIGetDebugInterface to enumerate what's alive
	}
}
