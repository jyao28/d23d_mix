#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "engine.h"

#include <windows.h>
#include <d3d11_1.h>
#pragma comment(lib, "d3d11.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <d2d1.h>
#include <d2d1_1.h>
// #include <wrl.h>

#include <string>

#include <assert.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static bool global_windowDidResize = false;

void AssertHResult(HRESULT hr, std::string&& errorMsg)
{
   if (FAILED(hr))
      throw std::exception(errorMsg.c_str());
}

// Create D3D11 Device and Context
UINT32 d3d11_engine::create_device_and_context()
{
   ID3D11Device* baseDevice = nullptr;
   ID3D11DeviceContext* baseDeviceContext = nullptr;
   D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
   UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
   creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

   HRESULT hResult = D3D11CreateDevice(0,
      D3D_DRIVER_TYPE_HARDWARE,
      0,
      creationFlags,
      featureLevels,
      ARRAYSIZE(featureLevels),
      D3D11_SDK_VERSION,
      &baseDevice,
      0,
      &baseDeviceContext);
   if (FAILED(hResult)) {
      MessageBoxA(0, "D3D11CreateDevice() failed", "Fatal Error", MB_OK);
      return GetLastError();
   }

   // Get 1.1 interface of D3D11 Device and Context
   hResult = baseDevice->QueryInterface(__uuidof(ID3D11Device1), (void**)&device);
   assert(SUCCEEDED(hResult));
   baseDevice->Release();

   hResult = baseDeviceContext->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&device_context);
   assert(SUCCEEDED(hResult));
   baseDeviceContext->Release();
   return 0;
}

// Set up debug layer to break on D3D11 errors
void d3d11_engine::setup_debug_layer()
{
   device->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug);
   if (d3dDebug)
   {
      ID3D11InfoQueue* d3dInfoQueue = nullptr;
      if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue)))
      {
         d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
         d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
         d3dInfoQueue->Release();
      }
      d3dDebug->Release();
   }
}

// Create Swap Chain
void d3d11_engine::create_swap_chain(HWND hwnd)
{
   // Get DXGI Factory (needed to create Swap Chain)
   IDXGIFactory2* dxgiFactory = nullptr;
   {
      IDXGIDevice1* dxgiDevice;
      HRESULT hResult = device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice);
      assert(SUCCEEDED(hResult));

      IDXGIAdapter* dxgiAdapter;
      hResult = dxgiDevice->GetAdapter(&dxgiAdapter);
      assert(SUCCEEDED(hResult));
      dxgiDevice->Release();

      DXGI_ADAPTER_DESC adapterDesc;
      dxgiAdapter->GetDesc(&adapterDesc);

      OutputDebugStringA("Graphics Device: ");
      OutputDebugStringW(adapterDesc.Description);

      hResult = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);
      assert(SUCCEEDED(hResult));
      dxgiAdapter->Release();
   }

   DXGI_SWAP_CHAIN_DESC1 d3d11SwapChainDesc = {};
   d3d11SwapChainDesc.Width = 0; // use window width
   d3d11SwapChainDesc.Height = 0; // use window height
   d3d11SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
   d3d11SwapChainDesc.SampleDesc.Count = 1;
   d3d11SwapChainDesc.SampleDesc.Quality = 0;
   d3d11SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   d3d11SwapChainDesc.BufferCount = 2;
   d3d11SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
   d3d11SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
   d3d11SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
   d3d11SwapChainDesc.Flags = 0;

   HRESULT hResult = dxgiFactory->CreateSwapChainForHwnd(device, hwnd, &d3d11SwapChainDesc, 0, 0, &swap_chain);
   assert(SUCCEEDED(hResult));

   dxgiFactory->Release();
}

// Create render target view
void d3d11_engine::create_render_target_view()
{
   ID3D11Texture2D* d3d11FrameBuffer = nullptr;
   HRESULT hResult = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&d3d11FrameBuffer);
   assert(SUCCEEDED(hResult));

   hResult = device->CreateRenderTargetView(d3d11FrameBuffer, 0, &render_target_view);
   assert(SUCCEEDED(hResult));
   d3d11FrameBuffer->Release();
}

// Create Vertex Shader
HRESULT d3d11_engine::create_vertex_shader()
{
   ID3DBlob* shaderCompileErrorsBlob = nullptr;
   HRESULT hResult = D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, &shaderCompileErrorsBlob);
   if (FAILED(hResult))
   {
      const char* errorString = nullptr;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
         errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
         errorString = (const char*)shaderCompileErrorsBlob->GetBufferPointer();
         shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error", MB_ICONERROR | MB_OK);
      return hResult;
   }

   hResult = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
   assert(SUCCEEDED(hResult));
   return hResult;
}

// Create Pixel Shader
HRESULT d3d11_engine::create_pixel_shader()
{
   ID3DBlob* psBlob = nullptr;
   ID3DBlob* shaderCompileErrorsBlob = nullptr;
   HRESULT hResult = D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlob, &shaderCompileErrorsBlob);
   if (FAILED(hResult))
   {
      const char* errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
         errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
         errorString = (const char*)shaderCompileErrorsBlob->GetBufferPointer();
         shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error", MB_ICONERROR | MB_OK);
      return hResult;
   }

   hResult = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
   assert(SUCCEEDED(hResult));
   psBlob->Release();
   return hResult;
}

// Create Input Layout
void d3d11_engine::create_input_layout()
{
   D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
   {
         { "POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
         { "TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
   };

   HRESULT hResult = device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
   assert(SUCCEEDED(hResult));
   vsBlob->Release();
}

// Create Vertex Buffer
void d3d11_engine::create_vertex_buffer()
{
   float vertexData[] = { // x, y, u, v
         -0.5f,  0.5f, 0.f, 0.f,
         0.5f, -0.5f, 1.f, 1.f,
         -0.5f, -0.5f, 0.f, 1.f,
         -0.5f,  0.5f, 0.f, 0.f,
         0.5f,  0.5f, 1.f, 0.f,
         0.5f, -0.5f, 1.f, 1.f
   };
   stride = 4 * sizeof(float);
   numVerts = sizeof(vertexData) / stride;
   offset = 0;

   D3D11_BUFFER_DESC vertexBufferDesc = {};
   vertexBufferDesc.ByteWidth = sizeof(vertexData);
   vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
   vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

   D3D11_SUBRESOURCE_DATA vertexSubresourceData = { vertexData };

   HRESULT hResult = device->CreateBuffer(&vertexBufferDesc, &vertexSubresourceData, &vertexBuffer);
   assert(SUCCEEDED(hResult));
}

// Create Sampler State
void d3d11_engine::create_sampler_state()
{
   D3D11_SAMPLER_DESC samplerDesc = {};
   samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
   samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
   samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
   samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
   samplerDesc.BorderColor[0] = 1.0f;
   samplerDesc.BorderColor[1] = 1.0f;
   samplerDesc.BorderColor[2] = 1.0f;
   samplerDesc.BorderColor[3] = 1.0f;
   samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

   device->CreateSamplerState(&samplerDesc, &samplerState);
}


void d3d11_engine::create_texture2d(std::string image_file, D3D11_USAGE usage, UINT bind_flags, UINT misc_flags)
{
   
   if (usage == D3D11_USAGE_IMMUTABLE)
   {
      texture = load_image(image_file);
   }
   else
   {
      ID3D11Texture2D* image = load_image(image_file);

      D3D11_TEXTURE2D_DESC texture_desc = {};
      image->GetDesc(&texture_desc);

      // Create Texture
      texture_desc.Usage = usage;
      texture_desc.BindFlags = bind_flags;
      texture_desc.MiscFlags = misc_flags;

      AssertHResult(device->CreateTexture2D(&texture_desc, NULL, &texture), "Fail to create texture");

      device_context->CopyResource(texture, image);

      image->Release();
   }

   if (bind_flags & D3D11_BIND_SHADER_RESOURCE)
   {
      AssertHResult(device->CreateShaderResourceView(texture, nullptr, &textureView), "Fail to create SRV");
   }

}

void d3d11_engine::create_texture2d()
{
   create_texture2d("testTexture.png", D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_SHARED);
   // create_texture2d("d2d_image.jpg", D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0);
}

// Load Image
ID3D11Texture2D* d3d11_engine::load_image(std::string image_file)
{
   ID3D11Texture2D* image;

   int texWidth, texHeight, texNumChannels;
   int texForceNumChannels = 4;
   unsigned char* testTextureBytes = stbi_load(image_file.c_str(), &texWidth, &texHeight,
      &texNumChannels, texForceNumChannels);
   assert(testTextureBytes);
   int texBytesPerRow = 4 * texWidth;

   // Create Texture
   D3D11_TEXTURE2D_DESC textureDesc = {};
   textureDesc.Width = texWidth;
   textureDesc.Height = texHeight;
   textureDesc.MipLevels = 1;
   textureDesc.ArraySize = 1;
   textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   textureDesc.SampleDesc.Count = 1;
   textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
   textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
   textureDesc.MiscFlags = 0;

   D3D11_SUBRESOURCE_DATA textureSubresourceData = {};
   textureSubresourceData.pSysMem = testTextureBytes;
   textureSubresourceData.SysMemPitch = texBytesPerRow;

   AssertHResult(device->CreateTexture2D(&textureDesc, &textureSubresourceData, &image), "Fail to create texture");

   free(testTextureBytes);

   return image;
}



void d3d11_engine::draw(D3D11_VIEWPORT& viewport)
{
   FLOAT backgroundColor[4] = { 0.1f, 0.2f, 0.6f, 1.0f };
   device_context->ClearRenderTargetView(render_target_view, backgroundColor);

   device_context->RSSetViewports(1, &viewport);

   device_context->OMSetRenderTargets(1, &render_target_view, nullptr);

   device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   device_context->IASetInputLayout(inputLayout);

   device_context->VSSetShader(vertexShader, nullptr, 0);
   device_context->PSSetShader(pixelShader, nullptr, 0);

   device_context->PSSetShaderResources(0, 1, &textureView);
   device_context->PSSetSamplers(0, 1, &samplerState);

   device_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

   device_context->Draw(numVerts, 0);
}

void d3d11_engine::resize()
{
   device_context->OMSetRenderTargets(0, 0, 0);
   render_target_view->Release();

   HRESULT res = swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
   assert(SUCCEEDED(res));

   ID3D11Texture2D* d3d11FrameBuffer;
   res = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&d3d11FrameBuffer);
   assert(SUCCEEDED(res));

   res = device->CreateRenderTargetView(d3d11FrameBuffer, NULL,
      &render_target_view);
   assert(SUCCEEDED(res));
   d3d11FrameBuffer->Release();
}

void d3d11_engine::init(HWND hwnd)
{
   create_device_and_context();
#ifdef _DEBUG
   setup_debug_layer();
#endif
   create_swap_chain(hwnd);
   create_render_target_view();
   if (create_vertex_shader() != S_OK)
      exit(1);
   if (create_pixel_shader() != S_OK)
      exit(1);
   create_input_layout();
   create_vertex_buffer();
   create_sampler_state();
   create_texture2d();
}

void d3d11_engine::update_image(d2d1_engine& d2d)
{

   if (!shared_texture)
   {
      HANDLE resourceHandle = d2d.shared_handle();

      AssertHResult(device->OpenSharedResource(resourceHandle, __uuidof(ID3D11Texture2D), (void**)&shared_texture), "Failed to open shared resource");
   }

   D3D11_TEXTURE2D_DESC d2dTextureDesc;
   D3D11_TEXTURE2D_DESC d3dTextureDesc;

   shared_texture->GetDesc(&d2dTextureDesc);
   texture->GetDesc(&d3dTextureDesc);

   D3D11_BOX d3dBox = { 0, 0, 0, d2dTextureDesc.Width, d2dTextureDesc.Height, 1 };

   device_context->CopySubresourceRegion(texture, 0, (d3dTextureDesc.Width - d2dTextureDesc.Width) / 2, (d3dTextureDesc.Height - d2dTextureDesc.Height) / 2, 0, shared_texture, 0, &d3dBox);
   //device_context->CopySubresourceRegion(texture, 0, 0, 0, 0, d2d_texture, 0, &d3dBox);

}


////////////////////////////////////////////////////////////////////////////////

void d2d1_engine::init(HWND hwnd)
{
   d3d_coinst.create_device_and_context();
#ifdef _DEBUG
   d3d_coinst.setup_debug_layer();
#endif
   d3d_coinst.create_swap_chain(hwnd);
   d3d_coinst.create_render_target_view();

   //d3d_coinst.create_texture2d("d2d_image.jpg", D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_SHARED);
   d3d_coinst.create_texture2d("d2d_image.jpg", D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_SHARED);

   d3d_coinst.device_context->Flush();

   {
      IDXGISurface* surface = nullptr;
      AssertHResult(get_texture2d()->QueryInterface(__uuidof(IDXGISurface), (void**)(&surface)), "Failed to get surface");

      IDXGIResource* dxgiResource = nullptr;
      AssertHResult(surface->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiResource), "Failed to get dxgi resource");

      AssertHResult(dxgiResource->GetSharedHandle(&shared_resource_handle), "Failed to get dxgi resource handle");

      dxgiResource->Release();
      surface->Release();
   }

}








////////////////////////////////////////////////////////////////////////////////


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
   LRESULT result = 0;
   switch (msg)
   {
   case WM_KEYDOWN:
   {
      if (wparam == VK_ESCAPE)
         DestroyWindow(hwnd);
      break;
   }
   case WM_DESTROY:
   {
      PostQuitMessage(0);
      break;
   }
   case WM_SIZE:
   {
      global_windowDidResize = true;
      break;
   }
   default:
      result = DefWindowProcW(hwnd, msg, wparam, lparam);
   }
   return result;
}


HWND open_window(HINSTANCE hInstance)
{
   HWND hwnd;

   WNDCLASSEXW winClass = {};
   winClass.cbSize = sizeof(WNDCLASSEXW);
   winClass.style = CS_HREDRAW | CS_VREDRAW;
   winClass.lpfnWndProc = &WndProc;
   winClass.hInstance = hInstance;
   winClass.hIcon = LoadIconW(0, IDI_APPLICATION);
   winClass.hCursor = LoadCursorW(0, IDC_ARROW);
   winClass.lpszClassName = L"MyWindowClass";
   winClass.hIconSm = LoadIconW(0, IDI_APPLICATION);

   if (!RegisterClassExW(&winClass)) {
      MessageBoxA(0, "RegisterClassEx failed", "Fatal Error", MB_OK);
      exit(GetLastError());
   }

   RECT initialRect = { 0, 0, 1024, 768 };
   AdjustWindowRectEx(&initialRect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_OVERLAPPEDWINDOW);
   LONG initialWidth = initialRect.right - initialRect.left;
   LONG initialHeight = initialRect.bottom - initialRect.top;

   hwnd = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW,
      winClass.lpszClassName,
      L"Hello World, Texture",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      initialWidth,
      initialHeight,
      0, 0, hInstance, 0);

   if (!hwnd) {
      MessageBoxA(0, "CreateWindowEx failed", "Fatal Error", MB_OK);
      exit(GetLastError());
   }

   return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
   // Open a window
   HWND hwnd = open_window(hInstance);

   d3d11_engine app;

   d2d1_engine d2d_image;
   d2d_image.init(hwnd);

   app.init(hwnd);

   RECT winRect;
   GetClientRect(hwnd, &winRect);
   D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)(winRect.right - winRect.left), (FLOAT)(winRect.bottom - winRect.top), 0.0f, 1.0f };


   // Main Loop
   bool isRunning = true;
   while (isRunning)
   {
      MSG msg = {};
      while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
      {
         if (msg.message == WM_QUIT)
            isRunning = false;

         TranslateMessage(&msg);
         DispatchMessageW(&msg);
      }

      if (global_windowDidResize)
      {
         GetClientRect(hwnd, &winRect);
         viewport = { 0.0f, 0.0f, (FLOAT)(winRect.right - winRect.left), (FLOAT)(winRect.bottom - winRect.top), 0.0f, 1.0f };

         app.resize();
         global_windowDidResize = false;
      }

      app.update_image(d2d_image);

      app.draw(viewport);

      app.present();
   }

   return 0;
}
