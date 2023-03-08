#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
//#define UNICODE
#include <windows.h>
#include <d3d11_1.h>
#pragma comment(lib, "d3d11.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <assert.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static bool global_windowDidResize = false;


class d3d11_app
{
private:
   ID3D11Device1* device;
   ID3D11DeviceContext1* device_context;
   IDXGISwapChain1* swap_chain;
   ID3D11Debug* d3dDebug = nullptr;
   ID3D11RenderTargetView* render_target_view;
   ID3DBlob* vsBlob = nullptr;
   ID3D11VertexShader* vertexShader = nullptr;
   ID3D11PixelShader* pixelShader = nullptr;
   ID3D11InputLayout* inputLayout = nullptr;
   ID3D11Buffer* vertexBuffer;
   UINT numVerts;
   UINT stride;
   UINT offset;
   ID3D11SamplerState* samplerState = nullptr;
   ID3D11Texture2D* texture = nullptr;
   ID3D11ShaderResourceView* textureView = nullptr;

public:

   // Create D3D11 Device and Context
   UINT32 create_device_and_context()
   {
      ID3D11Device* baseDevice = nullptr;
      ID3D11DeviceContext* baseDeviceContext = nullptr;
      D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
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
   void setup_debug_layer()
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
   void create_swap_chain(HWND hwnd)
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
      d3d11SwapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
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
   void create_render_target_view()
   {
      ID3D11Texture2D* d3d11FrameBuffer = nullptr;
      HRESULT hResult = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&d3d11FrameBuffer);
      assert(SUCCEEDED(hResult));

      hResult = device->CreateRenderTargetView(d3d11FrameBuffer, 0, &render_target_view);
      assert(SUCCEEDED(hResult));
      d3d11FrameBuffer->Release();
   }

   // Create Vertex Shader
   HRESULT create_vertex_shader()
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
   HRESULT create_pixel_shader()
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
   void create_input_layout()
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
   void create_vertex_buffer()
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
   void create_sampler_state()
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

   // Load Image
   void load_image()
   {
      int texWidth, texHeight, texNumChannels;
      int texForceNumChannels = 4;
      unsigned char* testTextureBytes = stbi_load("testTexture.png", &texWidth, &texHeight,
         &texNumChannels, texForceNumChannels);
      assert(testTextureBytes);
      int texBytesPerRow = 4 * texWidth;

      // Create Texture
      D3D11_TEXTURE2D_DESC textureDesc = {};
      textureDesc.Width = texWidth;
      textureDesc.Height = texHeight;
      textureDesc.MipLevels = 1;
      textureDesc.ArraySize = 1;
      textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
      textureDesc.SampleDesc.Count = 1;
      textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
      textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

      D3D11_SUBRESOURCE_DATA textureSubresourceData = {};
      textureSubresourceData.pSysMem = testTextureBytes;
      textureSubresourceData.SysMemPitch = texBytesPerRow;

      device->CreateTexture2D(&textureDesc, &textureSubresourceData, &texture);

      device->CreateShaderResourceView(texture, nullptr, &textureView);

      free(testTextureBytes);
   }


   virtual void draw(HWND hwnd)
   {
      FLOAT backgroundColor[4] = { 0.1f, 0.2f, 0.6f, 1.0f };
      device_context->ClearRenderTargetView(render_target_view, backgroundColor);

      RECT winRect;
      GetClientRect(hwnd, &winRect);
      D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)(winRect.right - winRect.left), (FLOAT)(winRect.bottom - winRect.top), 0.0f, 1.0f };
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

   void resize()
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

   void present()
   {
      swap_chain->Present(1, 0);
   }

   void init(HWND hwnd)
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
      load_image();
   }

};






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

    d3d11_app app;

    app.init(hwnd);

    // Main Loop
    bool isRunning = true;
    while(isRunning)
    {
        MSG msg = {};
        while(PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
        {
            if(msg.message == WM_QUIT)
                isRunning = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if(global_windowDidResize)
        {
           app.resize();
           global_windowDidResize = false;
        }

        app.draw(hwnd);

        app.present();
    }

    return 0;
}
