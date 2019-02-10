#include "stdafx.h"

#include <windows.h>

#include <shared/Detours/src/detours.h>

#include <d3d11.h>

#include <tchar.h>

#include <mutex>
#include <condition_variable>

#if defined(__CYGWIN32__)
#define UNITY_INTERFACE_API __stdcall
#define UNITY_INTERFACE_EXPORT __declspec(dllexport)
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(WINAPI_FAMILY)
#define UNITY_INTERFACE_API __stdcall
#define UNITY_INTERFACE_EXPORT __declspec(dllexport)
#elif defined(__MACH__) || defined(__ANDROID__) || defined(__linux__)
#define UNITY_INTERFACE_API
#define UNITY_INTERFACE_EXPORT
#else
#define UNITY_INTERFACE_API
#define UNITY_INTERFACE_EXPORT
#endif

typedef void (UNITY_INTERFACE_API * UnityRenderingEvent)(int eventId);
typedef void (UNITY_INTERFACE_API * UnityRenderingEventAndData)(int eventId, void* data);

LONG error = NO_ERROR;
HMODULE hD3d11Dll = NULL;

bool g_FbOverride = false;

typedef ULONG(STDMETHODCALLTYPE * AddReff_t)(ID3D11Device * This);
typedef ULONG(STDMETHODCALLTYPE * Release_t)(ID3D11Device * This);

ID3D11Device *pDevice = NULL;
ID3D11DeviceContext * pContext = NULL;
ID3D11Query * pQuery = NULL;

ULONG g_RefCOunt = 1;
AddReff_t True_AddRef = NULL;
Release_t True_Release;

ULONG STDMETHODCALLTYPE My_AddRef(ID3D11Device * This)
{
	g_RefCOunt = True_AddRef(This);

	return g_RefCOunt;
}

ULONG STDMETHODCALLTYPE My_Release(ID3D11Device * This)
{
	if (1 == g_RefCOunt)
	{
		if (pQuery)
		{
			pQuery->Release();
			pQuery = NULL;
		}

		pDevice = NULL;
	}

	g_RefCOunt = True_Release(This);

	return g_RefCOunt;
}

typedef HRESULT(STDMETHODCALLTYPE * CreateDeferredContext_t)(
	ID3D11Device * This,
	UINT ContextFlags,
	/* [annotation] */
	_COM_Outptr_opt_  ID3D11DeviceContext **ppDeferredContext);

CreateDeferredContext_t True_CreateDeferredContext;

HRESULT STDMETHODCALLTYPE My_CreateDeferredContext(
	ID3D11Device * This,
	UINT ContextFlags,
	/* [annotation] */
	_COM_Outptr_opt_  ID3D11DeviceContext **ppDeferredContext)
{
	HRESULT result = True_CreateDeferredContext(This, ContextFlags, ppDeferredContext);

	if (SUCCEEDED(result) && ppDeferredContext && *ppDeferredContext)
	{
		pContext = *ppDeferredContext;
	}

	return result;
}

typedef HRESULT(STDMETHODCALLTYPE * CreateTexture2D_t)(
	ID3D11Device * This,
	/* [annotation] */
	_In_  const D3D11_TEXTURE2D_DESC *pDesc,
	/* [annotation] */
	_In_reads_opt_(_Inexpressible_(pDesc->MipLevels * pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA *pInitialData,
	/* [annotation] */
	_COM_Outptr_opt_  ID3D11Texture2D **ppTexture2D);

CreateTexture2D_t True_CreateTexture2D;

HRESULT STDMETHODCALLTYPE My_CreateTexture2D(
	ID3D11Device * This,
	/* [annotation] */
	_In_  const D3D11_TEXTURE2D_DESC *pDesc,
	/* [annotation] */
	_In_reads_opt_(_Inexpressible_(pDesc->MipLevels * pDesc->ArraySize))  const D3D11_SUBRESOURCE_DATA *pInitialData,
	/* [annotation] */
	_COM_Outptr_opt_  ID3D11Texture2D **ppTexture2D)
{
	if (g_FbOverride && pDesc && ppTexture2D)
	{
		switch (pDesc->Format)
		{
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
			break;
		default:
			if (g_FbOverride)
			{
				g_FbOverride = false;

				D3D11_TEXTURE2D_DESC Desc = *pDesc;

				//Desc.Width = Width;
				//Desc.Height = Height;
				Desc.MipLevels = 1;
				Desc.ArraySize = 1;
				Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				Desc.SampleDesc.Count = 1;
				Desc.SampleDesc.Quality = 0;
				Desc.Usage = D3D11_USAGE_DEFAULT;
				Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				Desc.CPUAccessFlags = 0;
				Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;;


				HRESULT result = True_CreateTexture2D(This, &Desc, pInitialData, ppTexture2D);

				return result;
			}
			break;
		}
	}

	return True_CreateTexture2D(This, pDesc, pInitialData, ppTexture2D);
}


typedef HRESULT(WINAPI * D3D11CreateDevice_t)(
	_In_opt_        IDXGIAdapter        *pAdapter,
	D3D_DRIVER_TYPE     DriverType,
	HMODULE             Software,
	UINT                Flags,
	_In_opt_  const D3D_FEATURE_LEVEL   *pFeatureLevels,
	UINT                FeatureLevels,
	UINT                SDKVersion,
	_Out_opt_       ID3D11Device        **ppDevice,
	_Out_opt_       D3D_FEATURE_LEVEL   *pFeatureLevel,
	_Out_opt_       ID3D11DeviceContext **ppImmediateContext
	);

D3D11CreateDevice_t TrueD3D11CreateDevice = NULL;

HRESULT WINAPI MyD3D11CreateDevice(
	_In_opt_        IDXGIAdapter        *pAdapter,
	D3D_DRIVER_TYPE     DriverType,
	HMODULE             Software,
	UINT                Flags,
	_In_opt_  const D3D_FEATURE_LEVEL   *pFeatureLevels,
	UINT                FeatureLevels,
	UINT                SDKVersion,
	_Out_opt_       ID3D11Device        **ppDevice,
	_Out_opt_       D3D_FEATURE_LEVEL   *pFeatureLevel,
	_Out_opt_       ID3D11DeviceContext **ppImmediateContext
) {
	HRESULT result = TrueD3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

	if (IS_ERROR(result) || NULL == ppDevice)
	{
		error = E_FAIL;
		return result;
	}

	if (NULL == True_AddRef)
	{
		DWORD oldProtect;
		VirtualProtect(*ppDevice, sizeof(void *) * 27, PAGE_EXECUTE_READWRITE, &oldProtect);

		True_AddRef = (AddReff_t)*(void **)((*(char **)(*ppDevice)) + sizeof(void *) * 1);
		True_Release = (Release_t)*(void **)((*(char **)(*ppDevice)) + sizeof(void *) * 2);
		True_CreateTexture2D = (CreateTexture2D_t)*(void **)((*(char **)(*ppDevice)) + sizeof(void *) * 5);
		True_CreateDeferredContext = (CreateDeferredContext_t)*(void **)((*(char **)(*ppDevice)) + sizeof(void *) * 27);

		VirtualProtect(*ppDevice, sizeof(void *) * 27, oldProtect, NULL);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)True_AddRef, My_AddRef);
		DetourAttach(&(PVOID&)True_Release, My_Release);
		DetourAttach(&(PVOID&)True_CreateTexture2D, My_CreateTexture2D);
		DetourAttach(&(PVOID&)True_CreateDeferredContext, My_CreateDeferredContext);
		error = DetourTransactionCommit();
	}

	if (SUCCEEDED(error))
	{
		pDevice = *ppDevice;

		D3D11_QUERY_DESC queryDesc = {
			D3D11_QUERY_EVENT, 0
		};

		if (FAILED((*ppDevice)->CreateQuery(&queryDesc, &pQuery)))
		{
			pQuery = NULL;
		}
	}

	return result;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		hD3d11Dll = LoadLibrary(_T("d3d11.dll"));

		TrueD3D11CreateDevice = (D3D11CreateDevice_t)GetProcAddress(hD3d11Dll, "D3D11CreateDevice");

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)TrueD3D11CreateDevice, MyD3D11CreateDevice);
		error = DetourTransactionCommit();

	}
	break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&(PVOID&)TrueD3D11CreateDevice, MyD3D11CreateDevice);
		error = DetourTransactionCommit();

		FreeLibrary(hD3d11Dll);
	}
	break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) bool AfxHookUnityInit(int version) {
	return 2 == version && NO_ERROR == error;
}

extern "C" __declspec(dllexport) HANDLE AfxHookUnityGetSharedHandle(void * d3d11ResourcePtr)
{
	HANDLE result = NULL;

	if (d3d11ResourcePtr)
	{
		ID3D11Resource * resource = (ID3D11Resource *)d3d11ResourcePtr;

		IDXGIResource* pSurface;

		if (SUCCEEDED(resource->QueryInterface(__uuidof(IDXGIResource), (void**)&pSurface)))
		{
			if (FAILED(pSurface->GetSharedHandle(&result)))
			{
				result = NULL;
			}
			pSurface->Release();
		}
	}

	return result;
}

extern "C" __declspec(dllexport) void AfxHookUnityBeginCreateRenderTexture()
{
	g_FbOverride = true;
}

bool waitForGpuFinished = false;
std::mutex waitForGpuMutex;
std::condition_variable waitForGpuCondition;

extern "C"  __declspec(dllexport) void AfxHookUnityWaitOne()
{
	std::unique_lock<std::mutex> lock(waitForGpuMutex);
	waitForGpuCondition.wait(lock, [] { return waitForGpuFinished; });

	waitForGpuFinished = false;
}

extern "C" __declspec(dllexport) bool AfxHookUnityWaitForGPU()
{
	bool bOk = false;
	bool immediateContextUsed = false;

	if (!pContext)
	{
		immediateContextUsed = true;
		pDevice->GetImmediateContext(&pContext);
	}

	if (pDevice && pQuery)
	{
		pContext->Flush();

		pContext->End(pQuery);

		while (S_OK != pContext->GetData(pQuery, NULL, 0, 0))
			;

		bOk = true;
	}

	if (immediateContextUsed)
	{
		pContext->Release();
		pContext = NULL;
	}

	std::lock_guard<std::mutex> lock(waitForGpuMutex);
	waitForGpuFinished = true;

	waitForGpuCondition.notify_one();

	return bOk;
}


static void UNITY_INTERFACE_API OnRenderEvent(int eventId)
{
	if (1 == eventId)
	{
		AfxHookUnityWaitForGPU();
	}
}

static void UNITY_INTERFACE_API OnRenderEventAndData(int eventId, void* data)
{
	switch (eventId)
	{
	case 2:
		break;
	case 3:
		break;
	}

}

// Freely defined function to pass a callback to plugin-specific scripts
extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API AfxHookUnityGetRenderEventFunc()
{
	return OnRenderEvent;
}

// Freely defined function to pass a callback to plugin-specific scripts
extern "C" UnityRenderingEventAndData UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API AfxHookUnityGetRenderEventAndDataFunc()
{
	return OnRenderEventAndData;
}


