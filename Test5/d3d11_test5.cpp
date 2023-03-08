
// example how to set up D3D11 rendering

// set to 0 to create resizable window
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

// do you need depth buffer?
#define WINDOW_DEPTH 1

// do you need stencil buffer?
#define WINDOW_STENCIL 0

// use sRGB or MSAA for color buffer
// set 0 to disable, or 1 to enable sRGB
// typical values for MSAA are 2, 4, 8, 16, ...
// when enabled, D3D11 cannot use more modern lower-latency flip swap effect on Windows 8.1/10
// instead you can use sRGB/MSAA render target and copy it to non-sRGB window
#define WINDOW_SRGB 0
#define WINDOW_MSAA 0

// do you need vsync?
#define WINDOW_VSYNC 1

// keep this enabled when debugging
#define USE_DEBUG_MODE 1

#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <stddef.h>

#pragma comment (lib, "user32.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dxguid.lib")

#define SAFE_RELEASE(release, obj) if (obj) release##_Release(obj)

#define LOG_AND_RETURN_ERROR(hr, msg) do \
{                                        \
    if (FAILED(hr))                      \
    {                                    \
        LogWin32Error(hr, msg);          \
        return hr;                       \
    }                                    \
} while (0)

struct Vertex
{
   float x, y;
   float r, g, b;
};

static const struct Vertex vertices[] =
{
    {  0.0f,  0.5f, 1.f, 0.f, 0.f },
    {  0.5f, -0.5f, 0.f, 1.f, 0.f },
    { -0.5f, -0.5f, 0.f, 0.f, 1.f },
};

// You can compile shader to bytecode at build time, this will increase startup
// performance and also will avoid dependency on d3dcompiler dll file.
// To do so, put the shader source in shader.hlsl file and run these two commands:

// fxc.exe /nologo /T vs_4_0_level_9_0 /E vs /O3 /WX /Zpc /Ges /Fh d3d11_vshader.h /Vn d3d11_vshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl
// fxc.exe /nologo /T ps_4_0_level_9_0 /E ps /O3 /WX /Zpc /Ges /Fh d3d11_pshader.h /Vn d3d11_pshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl

// then set next setting to 1
#define USE_PRECOMPILED_SHADERS 0

#if USE_PRECOMPILED_SHADERS
#include "d3d11_vshader.h"
#include "d3d11_pshader.h"
#else
#pragma comment (lib, "d3dcompiler.lib")
static const char d3d11_shader[] =
"struct VS_INPUT                                \n"
"{                                              \n"
"  float2 pos : POSITION;                       \n"
"  float3 col : COLOR0;                         \n"
"};                                             \n"
"                                               \n"
"struct PS_INPUT                                \n"
"{                                              \n"
"  float4 pos : SV_POSITION;                    \n"
"  float3 col : COLOR0;                         \n"
"};                                             \n"
"                                               \n"
"PS_INPUT vs(VS_INPUT input)                    \n"
"{                                              \n"
"  PS_INPUT output;                             \n"
"  output.pos = float4(input.pos.xy, 0.f, 1.f); \n"
"  output.col = input.col;                      \n"
"  return output;                               \n"
"}                                              \n"
"                                               \n"
"float4 ps(PS_INPUT input) : SV_Target          \n"
"{                                              \n"
"  return float4(input.col, 1.f);               \n"
"}                                              \n";
#endif

// is window visible?
// if this is 0 you can skip rendering part in your code to save time
static int render_occluded;

static IDXGISwapChain* render_swapchain;
static ID3D11Device* render_device;
static ID3D11DeviceContext* render_context;
static ID3D11DeviceContext1* render_context1;
static ID3D11RenderTargetView* render_window_rtview;
#if WINDOW_DEPTH || WINDOW_STENCIL
static ID3D11DepthStencilView* render_window_dpview;
#endif
static HANDLE render_frame_latency_wait;

static ID3D11RasterizerState* render_raster_state;
static ID3D11DepthStencilState* render_depthstencil_state;
static ID3D11BlendState* render_blend_state;
static ID3D11PixelShader* render_pixel_shader;
static ID3D11VertexShader* render_vertex_shader;
static ID3D11InputLayout* render_input_layout;
static ID3D11Buffer* render_vertex_buffer;

static void LogWin32Error(DWORD err, const char* msg)
{
   OutputDebugStringA(msg);
   OutputDebugStringA("!\n");

   LPWSTR str;
   if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
      err, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), (LPWSTR)&str, 0, NULL))
   {
      OutputDebugStringW(str);
      LocalFree(str);
   }
}
static void LogWin32LastError(const char* msg)
{
   LogWin32Error(GetLastError(), msg);
}

static HRESULT FatalDeviceLostError()
{
   MessageBoxW(NULL, L"Cannot recreate D3D11 device, it is reset or removed!", L"Error", MB_ICONEXCLAMATION);
   return E_FAIL;
}

// called when device & all d3d resources needs to be released
// can happen multiple times (e.g. after device is removed/reset)
static void RenderDestroy()
{
   if (render_context)
   {
      ID3D11DeviceContext1_ClearState(render_context);
   }

   SAFE_RELEASE(ID3D11Buffer, render_vertex_buffer);
   SAFE_RELEASE(ID3D11InputLayout, render_input_layout);
   SAFE_RELEASE(ID3D11VertexShader, render_vertex_shader);
   SAFE_RELEASE(ID3D11PixelShader, render_pixel_shader);
   SAFE_RELEASE(ID3D11RasterizerState, render_raster_state);
   SAFE_RELEASE(ID3D11DepthStencilState, render_depthstencil_state);
   SAFE_RELEASE(ID3D11BlendState, render_blend_state);

#if WINDOW_DEPTH || WINDOW_STENCIL
   SAFE_RELEASE(ID3D11DepthStencilView, render_window_dpview);
#endif
   SAFE_RELEASE(ID3D11RenderTargetView, render_window_rtview);
   SAFE_RELEASE(ID3D11DeviceContext, render_context);
   SAFE_RELEASE(ID3D11Device, render_device);
   SAFE_RELEASE(IDXGISwapChain, render_swapchain);

   render_context1 = NULL;
   render_frame_latency_wait = NULL;
}

// called any time device needs to be created
// can happen multiple times (e.g. after device is removed/reset)
static HRESULT RenderCreate(HWND wnd)
{
   HRESULT hr;
   D3D_FEATURE_LEVEL level;

   // device, context
   {
      UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#if USE_DEBUG_MODE
      flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

      if (FAILED(hr = D3D11CreateDevice(
         NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION,
         &render_device, &level, &render_context)))
      {
         if (FAILED(hr = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, NULL, 0, D3D11_SDK_VERSION,
            &render_device, &level, &render_context)))
         {
            LOG_AND_RETURN_ERROR(hr, "D3D11CreateDevice failed");
         }
      }

      hr = ID3D11DeviceContext_QueryInterface(render_context, IID_ID3D11DeviceContext1, (void**)&render_context1);
      if (SUCCEEDED(hr))
      {
         // using ID3D11DeviceContext1 to discard render target
         ID3D11DeviceContext_Release(render_context);
         render_context = (ID3D11DeviceContext*)render_context1;
      }
   }

   // swap chain
   {
      IDXGIFactory* factory;
      if (FAILED(hr = CreateDXGIFactory(IID_IDXGIFactory, (void**)&factory)))
      {
         LOG_AND_RETURN_ERROR(hr, "CreateDXGIFactory failed");
      }

      DXGI_SWAP_CHAIN_DESC desc = {};
#if WINDOW_SRGB
      desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
#else
      desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
      desc.BufferDesc.RefreshRate.Numerator = 60;
      desc.BufferDesc.RefreshRate.Denominator = 1;

#if WINDOW_MSAA
      desc.SampleDesc.Count = WINDOW_MSAA;
#else
      desc.SampleDesc.Count = 1;
#endif
      desc.SampleDesc.Quality = 0;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.OutputWindow = wnd;
      desc.Windowed = TRUE;

#if !WINDOW_SRGB && !WINDOW_MSAA
      // Windows 10 and up
      desc.BufferCount = 2;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
      if (FAILED(IDXGIFactory_CreateSwapChain(factory, (IUnknown*)render_device, &desc, &render_swapchain)))
      {
         // Windows 8.1
         desc.BufferCount = 2;
         desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
         desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
         hr = IDXGIFactory_CreateSwapChain(factory, (IUnknown*)render_device, &desc, &render_swapchain);
      }
#else
      hr = E_FAIL;
#endif
      if (FAILED(hr))
      {
         // older Windows
         desc.BufferCount = 1;
         desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
         desc.Flags = 0;
         hr = IDXGIFactory_CreateSwapChain(factory, (IUnknown*)render_device, &desc, &render_swapchain);
         LOG_AND_RETURN_ERROR(hr, "IDXGIFactory::CreateSwapChain failed");
      }

      if (desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
      {
         IDXGISwapChain2* swapchain2;
         if (SUCCEEDED(hr = IDXGISwapChain_QueryInterface(render_swapchain, IID_IDXGISwapChain2, (void**)&swapchain2)))
         {
            // using IDXGISwapChain2 for frame latency control
            render_frame_latency_wait = IDXGISwapChain2_GetFrameLatencyWaitableObject(swapchain2);
            IDXGISwapChain2_Release(swapchain2);
         }
      }

      hr = IDXGIFactory_MakeWindowAssociation(factory, wnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
      LOG_AND_RETURN_ERROR(hr, "IDXGIFactory::MakeWindowAssociation failed");

      IDXGIFactory_Release(factory);
   }

   // rasterizer state
   {
      D3D11_RASTERIZER_DESC desc = {};

      desc.FillMode = D3D11_FILL_SOLID;
      desc.CullMode = D3D11_CULL_BACK;
      desc.FrontCounterClockwise = FALSE;
      desc.DepthBias = 0;
      desc.DepthBiasClamp = 0;
      desc.SlopeScaledDepthBias = 0.f;
      desc.DepthClipEnable = TRUE;
      desc.ScissorEnable = FALSE;
      desc.MultisampleEnable = WINDOW_MSAA > 0;
      desc.AntialiasedLineEnable = FALSE;


      hr = ID3D11Device_CreateRasterizerState(render_device, &desc, &render_raster_state);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateRasterizerState failed");
   }

#if WINDOW_DEPTH || WINDOW_STENCIL
   // depth & stencil state
   {
      D3D11_DEPTH_STENCIL_DESC desc = {};

      desc.DepthEnable = WINDOW_DEPTH ? TRUE : FALSE;
      desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      desc.DepthFunc = D3D11_COMPARISON_LESS;
      desc.StencilEnable = FALSE; // if you need stencil, set up it here
      desc.StencilReadMask = 0;
      desc.StencilWriteMask = 0;


      hr = ID3D11Device_CreateDepthStencilState(render_device, &desc, &render_depthstencil_state);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateDepthStencilState failed");
   }
#endif

   // blend state
   {
      D3D11_BLEND_DESC desc = {};

      desc.AlphaToCoverageEnable = FALSE;
      desc.IndependentBlendEnable = FALSE;
      desc.RenderTarget[0].BlendEnable = FALSE;
      desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
      desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
      desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
      desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
      desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
      desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
      desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

      hr = ID3D11Device_CreateBlendState(render_device, &desc, &render_blend_state);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateBlendState failed");
   }

#if !USE_PRECOMPILED_SHADERS
   UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS
#if USE_DEBUG_MODE
      | D3DCOMPILE_OPTIMIZATION_LEVEL0 | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
      | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
#endif

   // vertex shader & input layout
   {
      D3D11_INPUT_ELEMENT_DESC layout[] =
      {
          { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(struct Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(struct Vertex, r), D3D11_INPUT_PER_VERTEX_DATA, 0 },
      };

      ID3DBlob* code = NULL;
      const void* vshader;
      size_t vshader_size;

#if USE_PRECOMPILED_SHADERS
      vshader = d3d11_vshader;
      vshader_size = sizeof(d3d11_vshader);
#else
      ID3DBlob* error;
      hr = D3DCompile(d3d11_shader, sizeof(d3d11_shader) - 1, NULL, NULL, NULL,
         "vs", "vs_4_0_level_9_0", shader_flags, 0, &code, &error);
      if (FAILED(hr))
      {
         const void* data = ID3D10Blob_GetBufferPointer(error);
         size_t size = ID3D10Blob_GetBufferSize(error);
         char msg[1024];
         lstrcpynA(msg, (LPCSTR)data, (int)size);
         msg[size] = 0;
         OutputDebugStringA(msg);
         ID3D10Blob_Release(error);
         LOG_AND_RETURN_ERROR(hr, "D3DCompile vs failed");
      }
      vshader = ID3D10Blob_GetBufferPointer(code);
      vshader_size = ID3D10Blob_GetBufferSize(code);
#endif

      hr = ID3D11Device_CreateVertexShader(render_device, vshader, vshader_size, NULL, &render_vertex_shader);
      if (FAILED(hr))
      {
         SAFE_RELEASE(ID3D10Blob, code);
         LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateVertexShader failed");
      }

      hr = ID3D11Device_CreateInputLayout(render_device, layout, _countof(layout),
         vshader, vshader_size, &render_input_layout);
      SAFE_RELEASE(ID3D10Blob, code);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateInputLayout failed");
   }

   // pixel shader
   {
      ID3DBlob* code = NULL;
      const void* pshader;
      size_t pshader_size;

#if USE_PRECOMPILED_SHADERS
      pshader = d3d11_pshader;
      pshader_size = sizeof(d3d11_pshader);
#else
      ID3DBlob* error;
      hr = D3DCompile(d3d11_shader, sizeof(d3d11_shader) - 1, NULL, NULL, NULL,
         "ps", "ps_4_0_level_9_0", shader_flags, 0, &code, &error);
      if (FAILED(hr))
      {
         const void* data = ID3D10Blob_GetBufferPointer(error);
         size_t size = ID3D10Blob_GetBufferSize(error);
         char msg[1024];
         lstrcpynA(msg, (LPCSTR)data, (int)size);
         msg[size] = 0;
         OutputDebugStringA(msg);
         ID3D10Blob_Release(error);
         LOG_AND_RETURN_ERROR(hr, "D3DCompile ps failed");
      }
      pshader = ID3D10Blob_GetBufferPointer(code);
      pshader_size = ID3D10Blob_GetBufferSize(code);
#endif

      hr = ID3D11Device_CreatePixelShader(render_device, pshader, pshader_size, NULL, &render_pixel_shader);
      SAFE_RELEASE(ID3D10Blob, code);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreatePixelShader failed");
   }

   // vertex buffer
   {
      D3D11_BUFFER_DESC desc = {};

      desc.ByteWidth = sizeof(vertices);
      desc.Usage = D3D11_USAGE_IMMUTABLE;
      desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;


      D3D11_SUBRESOURCE_DATA data = {};

      data.pSysMem = vertices;


      hr = ID3D11Device_CreateBuffer(render_device, &desc, &data, &render_vertex_buffer);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateBuffer failed");
   }

   return S_OK;
}

// called when device is reset or removed, recreate it
static HRESULT RecreateDevice(HWND wnd)
{
   RenderDestroy();
   HRESULT hr = RenderCreate(wnd);
   if (FAILED(hr))
   {
      RenderDestroy();
   }
   return hr;
}

// called when window is resized
static HRESULT RenderResize(HWND wnd, int width, int height)
{
   if (width == 0 || height == 0)
   {
      return S_OK;
   }

   if (render_window_rtview)
   {
      ID3D11DeviceContext_OMSetRenderTargets(render_context, 0, NULL, NULL);
      ID3D11RenderTargetView_Release(render_window_rtview);
      render_window_rtview = NULL;
   }

#if WINDOW_DEPTH || WINDOW_STENCIL
   if (render_window_dpview)
   {
      ID3D11DepthStencilView_Release(render_window_dpview);
      render_window_dpview = NULL;
   }
#endif

   UINT flags = render_frame_latency_wait ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
   HRESULT hr = IDXGISwapChain_ResizeBuffers(render_swapchain, 0, width, height, DXGI_FORMAT_UNKNOWN, flags);
   if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
   {
      if (FAILED(RecreateDevice(wnd)))
      {
         return FatalDeviceLostError();
      }
   }
   else
   {
      LOG_AND_RETURN_ERROR(hr, "IDXGISwapChain::ResizeBuffers failed");
   }

   ID3D11Texture2D* window_buffer;
   hr = IDXGISwapChain_GetBuffer(render_swapchain, 0, IID_ID3D11Texture2D, (void**)&window_buffer);
   LOG_AND_RETURN_ERROR(hr, "IDXGISwapChain::GetBuffer failed");

   hr = ID3D11Device_CreateRenderTargetView(render_device, (ID3D11Resource*)window_buffer, NULL, &render_window_rtview);
   ID3D11Texture2D_Release(window_buffer);
   LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateRenderTargetView failed");

#if WINDOW_DEPTH || WINDOW_STENCIL
   {
      D3D11_TEXTURE2D_DESC desc = {};

      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = (WINDOW_STENCIL || ID3D11Device_GetFeatureLevel(render_device) < D3D_FEATURE_LEVEL_10_0)
         ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;

#if WINDOW_MSAA
      desc.SampleDesc.Count = WINDOW_MSAA;
#else
      desc.SampleDesc.Count = 1;
#endif
      desc.SampleDesc.Quality = 0;

      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;


      ID3D11Texture2D* depth_stencil;
      hr = ID3D11Device_CreateTexture2D(render_device, &desc, NULL, &depth_stencil);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateTexture2D failed");

      hr = ID3D11Device_CreateDepthStencilView(render_device, (ID3D11Resource*)depth_stencil, NULL, &render_window_dpview);
      ID3D11Texture2D_Release(depth_stencil);
      LOG_AND_RETURN_ERROR(hr, "ID3D11Device::CreateDepthStencilView failed");
   }
#endif

   D3D11_VIEWPORT viewport = {};

   viewport.TopLeftX = 0.f;
   viewport.TopLeftY = 0.f;
   viewport.Width = (float)width;
   viewport.Height = (float)height;
   viewport.MinDepth = 0.f;
   viewport.MaxDepth = 1.f;

   ID3D11DeviceContext_RSSetViewports(render_context, 1, &viewport);

   return S_OK;
}

// called at end of frame
static HRESULT RenderPresent(HWND wnd)
{
   HRESULT hr = S_OK;
   if (render_occluded)
   {
      hr = IDXGISwapChain_Present(render_swapchain, 0, DXGI_PRESENT_TEST);
      if (SUCCEEDED(hr) && hr != DXGI_STATUS_OCCLUDED)
      {
         // DXGI window is back to normal, resuming rendering
         render_occluded = 0;
      }
   }

   if (!render_occluded)
   {
      hr = IDXGISwapChain_Present(render_swapchain, WINDOW_VSYNC, 0);
   }

   if (hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_REMOVED)
   {
      if (FAILED(RecreateDevice(wnd)))
      {
         return FatalDeviceLostError();
      }

      RECT rect;
      if (!GetClientRect(wnd, &rect))
      {
         LogWin32LastError("GetClientRect failed");
      }
      else
      {
         RenderResize(wnd, rect.right - rect.left, rect.bottom - rect.top);
      }
   }
   else if (hr == DXGI_STATUS_OCCLUDED)
   {
      // DXGI window is occluded, skipping rendering
      render_occluded = 1;
   }
   else
   {
      LOG_AND_RETURN_ERROR(hr, "IDXGISwapChain::Present failed");
   }

   if (render_occluded)
   {
      Sleep(10);
   }
   else
   {
      if (render_context1)
      {
         ID3D11DeviceContext1_DiscardView(render_context1, (ID3D11View*)render_window_rtview);
      }
   }

   return S_OK;
}

// this is where rendering happens
static void RenderFrame()
{
   if (!render_occluded)
   {
      if (render_frame_latency_wait)
      {
         WaitForSingleObjectEx(render_frame_latency_wait, INFINITE, TRUE);
      }

#if WINDOW_DEPTH || WINDOW_STENCIL
      ID3D11DeviceContext_OMSetRenderTargets(render_context, 1, &render_window_rtview, render_window_dpview);
      ID3D11DeviceContext_OMSetDepthStencilState(render_context, render_depthstencil_state, 0);
      ID3D11DeviceContext_ClearDepthStencilView(render_context, render_window_dpview, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
#else
      ID3D11DeviceContext_OMSetRenderTargets(render_context, 1, &render_window_rtview, NULL);
#endif

      // clear background
      FLOAT clear_color[] = { 100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 1.f };
      ID3D11DeviceContext_ClearRenderTargetView(render_context, render_window_rtview, clear_color);

      // draw a triangle
      const UINT stride = sizeof(struct Vertex);
      const UINT offset = 0;
      ID3D11DeviceContext_IASetInputLayout(render_context, render_input_layout);
      ID3D11DeviceContext_IASetVertexBuffers(render_context, 0, 1, &render_vertex_buffer, &stride, &offset);
      ID3D11DeviceContext_IASetPrimitiveTopology(render_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      ID3D11DeviceContext_VSSetShader(render_context, render_vertex_shader, NULL, 0);
      ID3D11DeviceContext_PSSetShader(render_context, render_pixel_shader, NULL, 0);
      ID3D11DeviceContext_RSSetState(render_context, render_raster_state);
      ID3D11DeviceContext_OMSetBlendState(render_context, render_blend_state, NULL, ~0U);
      ID3D11DeviceContext_Draw(render_context, _countof(vertices), 0);
   }
}

static LRESULT CALLBACK WindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
   switch (msg)
   {
   case WM_CREATE:
      if (FAILED(RenderCreate(wnd)))
      {
         return -1;
      }
      return 0;

   case WM_DESTROY:
      RenderDestroy();
      PostQuitMessage(0);
      return 0;

   case WM_SIZE:
      if (FAILED(RenderResize(wnd, LOWORD(lparam), HIWORD(lparam))))
      {
         DestroyWindow(wnd);
      }
      return 0;
   }
   return DefWindowProcW(wnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int cmd_show)
{
   WNDCLASSEXW wc = {};

   wc.cbSize = sizeof(wc);
   wc.lpfnWndProc = WindowProc;
   wc.hInstance = instance;
   wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
   wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
   wc.lpszClassName = L"d3d11_window_class";


   if (!RegisterClassExW(&wc))
   {
      LogWin32LastError("RegisterClassEx failed");
   }
   else
   {
      int width = CW_USEDEFAULT;
      int height = CW_USEDEFAULT;

      DWORD exstyle = WS_EX_APPWINDOW;
      DWORD style = WS_OVERLAPPEDWINDOW;

      if (WINDOW_WIDTH && WINDOW_HEIGHT)
      {
         style &= ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

         RECT rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
         if (!AdjustWindowRectEx(&rect, style, FALSE, exstyle))
         {
            LogWin32LastError("AdjustWindowRectEx failed");
            style = WS_OVERLAPPEDWINDOW;
         }
         else
         {
            width = rect.right - rect.left;
            height = rect.bottom - rect.top;
         }
      }

      HWND wnd = CreateWindowExW(exstyle, wc.lpszClassName, L"D3D11 Window",
         style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
         NULL, NULL, wc.hInstance, NULL);
      if (!wnd)
      {
         LogWin32LastError("CreateWindow failed");
      }
      else
      {
         for (;;)
         {
            MSG msg;
            if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
               if (msg.message == WM_QUIT)
               {
                  break;
               }
               TranslateMessage(&msg);
               DispatchMessageW(&msg);
               continue;
            }

            RenderFrame();

            if (FAILED(RenderPresent(wnd)))
            {
               break;
            }
         }
      }

      UnregisterClassW(wc.lpszClassName, wc.hInstance);
   }

   return 0;
}


