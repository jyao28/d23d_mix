#include <Windows.h>
#include <string>
#include <d3d11.h>
#include <d2d1.h>
#include <wrl.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3dcompiler.lib")
using namespace std;
using namespace Microsoft::WRL;

// Globals

HWND g_hWnd;
// D3D11
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTarget;
ComPtr<ID3D11Buffer> g_triangleVertexBuffer;
// D2D
ComPtr<ID2D1RenderTarget> g_renderTarget2D;
ComPtr<ID2D1Factory> g_factory2D;

// Utilities

constexpr const char* VERTEX_SHADER_CODE =
R"(
float4 main(float2 pos : POSITION) : SV_Position
{
    return float4(pos, 0.0f, 1.0f);
}
)";

constexpr const char* PIXEL_SHADER_CODE =
R"(
float4 main() : SV_Target
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)";

struct Vector2f
{
   float x, y;
   Vector2f() : x(0.0f), y(0.0f) { }
   Vector2f(float x, float y) : x(x), y(y) { }
};

void AssertHResult(HRESULT hr, string errorMsg)
{
   if (FAILED(hr))
      throw std::exception(errorMsg.c_str());
}

void CompileShaderFromString(string code, string shaderType, ID3DBlob** output)
{
   AssertHResult(D3DCompile(
      code.c_str(),
      code.length(),
      nullptr,
      nullptr,
      nullptr,
      "main",
      shaderType.c_str(),
#ifdef _DEBUG
      D3DCOMPILE_DEBUG |
#else
      D3DCOMPILE_OPTIMIZATION_LEVEL3 |
#endif
      D3DCOMPILE_ENABLE_STRICTNESS,
      NULL,
      output,
      nullptr
   ), "Failed to compile shader");
}

// Graphics stuff

void InitializeD2D()
{
   // Get swap chain surface
   ComPtr<IDXGISurface> surface;
   AssertHResult(g_swapChain->GetBuffer(
      0,
      __uuidof(IDXGISurface),
      static_cast<void**>(&surface)
   ), "Failed to get surface of swap chain");

   // Create factory
   AssertHResult(D2D1CreateFactory<ID2D1Factory>(
      D2D1_FACTORY_TYPE_SINGLE_THREADED,
      &g_factory2D
      ), "Failed to create factory");

   // Create render target
   D2D1_RENDER_TARGET_PROPERTIES rtDesc = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_HARDWARE,
      D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)  // D2D1_BLEND_MODE_PREMULTIPLIED) //
   );
   AssertHResult(g_factory2D->CreateDxgiSurfaceRenderTarget(
      surface.Get(),
      &rtDesc,
      &g_renderTarget2D
   ), "Failed to create D2D render target");

}

void InitializeD3D()
{
   // Get window dimensions
   RECT rect{};
   GetClientRect(g_hWnd, &rect);
   float width = static_cast<float>(rect.right - rect.left);
   float height = static_cast<float>(rect.bottom - rect.top);

   // Create device, context, and swapchain
   DXGI_SWAP_CHAIN_DESC scDesc{};
   scDesc.BufferCount = 1;
   scDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
   scDesc.BufferDesc.Width = static_cast<UINT>(width);
   scDesc.BufferDesc.Height = static_cast<UINT>(height);
   scDesc.BufferDesc.RefreshRate.Numerator = 0;
   scDesc.BufferDesc.RefreshRate.Denominator = 0;
   scDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
   scDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
   scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   scDesc.Flags = NULL;
   scDesc.OutputWindow = g_hWnd;
   scDesc.SampleDesc.Count = 1;
   scDesc.SampleDesc.Quality = 0;
   scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
   scDesc.Windowed = true;
   AssertHResult(D3D11CreateDeviceAndSwapChain(
      nullptr,
      D3D_DRIVER_TYPE_HARDWARE,
      NULL,
#ifdef _DEBUG
      D3D11_CREATE_DEVICE_DEBUG |
#endif
      0x0e, //D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      nullptr,
      NULL,
      D3D11_SDK_VERSION,
      &scDesc,
      &g_swapChain,
      &g_device,
      nullptr,
      &g_context
   ), "Failed to create device and swapchain");

   D3D11_BLEND_DESC blendDesc = {};
   blendDesc.RenderTarget[0].BlendEnable = true;
   blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_ALPHA;
   blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_DEST_ALPHA;
   blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
   blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
   blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
   blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
   blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

   ID3D11BlendState* blendState;
   g_device->CreateBlendState(&blendDesc, &blendState);

   float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
   UINT sampleMask = 0xffffffff;
   g_context->OMSetBlendState(blendState, blendFactor, sampleMask);

   // Create render target
   ComPtr<ID3D11Resource> backBuffer;
   AssertHResult(g_swapChain->GetBuffer(
      0,
      __uuidof(ID3D11Resource),
      static_cast<void**>(&backBuffer)
   ), "Failed to get back buffer of swapchain");
   AssertHResult(g_device->CreateRenderTargetView(
      backBuffer.Get(),
      nullptr,
      &g_renderTarget
   ), "Failed to create render target view");

   // Bind render target
   g_context->OMSetRenderTargets(
      1,
      g_renderTarget.GetAddressOf(),
      nullptr
   );

   // Bind viewport
   D3D11_VIEWPORT viewport{};
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   viewport.TopLeftX = 0.0f;
   viewport.TopLeftY = 0.0f;
   viewport.Width = width;
   viewport.Height = height;
   g_context->RSSetViewports(
      1,
      &viewport
   );
}

void InitializeD3DTriangle()
{
   // Create vertex buffer
   Vector2f vertices[3] =
   {
       Vector2f(-0.5f, -0.5f),
       Vector2f(0.0f, 0.5f),
       Vector2f(0.5f, -0.5f)
   };
   D3D11_BUFFER_DESC vbDesc{};
   vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
   vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vector2f) * 3);
   vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
   vbDesc.MiscFlags = NULL;
   vbDesc.StructureByteStride = sizeof(Vector2f);
   vbDesc.Usage = D3D11_USAGE_DYNAMIC;
   D3D11_SUBRESOURCE_DATA vbData{};
   vbData.pSysMem = vertices;
   AssertHResult(g_device->CreateBuffer(
      &vbDesc,
      &vbData,
      &g_triangleVertexBuffer
   ), "Failed to create vertex buffer");

   // Bind vertex buffer
   const UINT offset = 0;
   const UINT stride = sizeof(Vector2f);
   g_context->IASetVertexBuffers(
      0,
      1,
      g_triangleVertexBuffer.GetAddressOf(),
      &stride,
      &offset
   );

   // Create and bind vertex shader
   ComPtr<ID3DBlob> vsBlob;
   ComPtr<ID3D11VertexShader> vertexShader;
   CompileShaderFromString(
      VERTEX_SHADER_CODE,
      "vs_4_0",
      &vsBlob
   );
   AssertHResult(g_device->CreateVertexShader(
      vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(),
      nullptr,
      &vertexShader
   ), "Failed to create vertex shader");
   g_context->VSSetShader(
      vertexShader.Get(),
      nullptr,
      NULL
   );

   // Create and bind pixel shader
   ComPtr<ID3DBlob> pxBlob;
   ComPtr<ID3D11PixelShader> pixelShader;
   CompileShaderFromString(
      PIXEL_SHADER_CODE,
      "ps_4_0",
      &pxBlob
   );
   AssertHResult(g_device->CreatePixelShader(
      pxBlob->GetBufferPointer(),
      pxBlob->GetBufferSize(),
      nullptr,
      &pixelShader
   ), "Failed to create pixel shader");
   g_context->PSSetShader(
      pixelShader.Get(),
      nullptr,
      NULL
   );

   // Set topology
   g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

   // Create input layout
   ComPtr<ID3D11InputLayout> inputLayout;
   D3D11_INPUT_ELEMENT_DESC ilDesc{};
   ilDesc.AlignedByteOffset = 0;
   ilDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
   ilDesc.SemanticName = "POSITION";
   ilDesc.SemanticIndex = 0;
   ilDesc.InputSlot = 0;
   ilDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
   ilDesc.InstanceDataStepRate = 0;
   AssertHResult(g_device->CreateInputLayout(
      &ilDesc,
      1,
      vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(),
      &inputLayout
   ), "Failed to create input layout");

   // Bind input layout
   g_context->IASetInputLayout(inputLayout.Get());
}

void D2DDraw()
{
   // Begin frame
   g_renderTarget2D->BeginDraw();

   // Clear screen to black
   // D2D1_COLOR_F bgColour = { 0.0f, 0.0f, 0.0f, 1.0f };
   // g_renderTarget2D->Clear(bgColour);


   // Draw D2D rectangle
   D2D_RECT_F rect{};
   rect.bottom = 500;
   rect.top = 300;
   rect.left = 100;
   rect.right = 700;
   D2D1_COLOR_F rectColour = { 0.0f, 1.0f, 0.0f, 0.75f };
   ComPtr<ID2D1SolidColorBrush> brush;
   g_renderTarget2D->CreateSolidColorBrush(
      rectColour,
      &brush
   );
   g_renderTarget2D->FillRectangle(
      rect,
      brush.Get()
   );

   // End frame
   g_renderTarget2D->EndDraw();

}

void Update()
{
   // Draw D3D triangle
   g_context->Draw(
      3,
      0
   );

   // Draw D2D rectangle
   D2DDraw();

   // Swap buffer
   AssertHResult(g_swapChain->Present(
      1,
      NULL
   ), "Failed to present swapchain");

}

// Windows

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
   switch (msg)
   {
   case WM_DESTROY:
      PostQuitMessage(0);
      break;
   default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
   }
   return 0;
}

void InitializeWindow(HINSTANCE hInst, int width, int height)
{
   // Register window class
   WNDCLASSEXW wc{};
   wc.cbSize = sizeof(WNDCLASSEXW);
   wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
   wc.hInstance = hInst;
   wc.lpfnWndProc = WndProc;
   wc.lpszClassName = L"MainWindow";
   wc.style = CS_OWNDC;
   RegisterClassExW(&wc);

   // Adjust width and height to be client area instead of window area
   RECT rc{};
   rc.left = 0;
   rc.top = 0;
   rc.right = width;
   rc.bottom = height;
   constexpr auto ws = WS_OVERLAPPEDWINDOW;
   AdjustWindowRectEx(
      &rc,
      ws,
      false,
      NULL
   );

   // Instantiate and show window
   g_hWnd = CreateWindowExW(
      NULL,
      L"MainWindow",
      L"Window Title",
      ws,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      static_cast<int>(rc.right - rc.left),
      static_cast<int>(rc.bottom - rc.top),
      NULL,
      NULL,
      hInst,
      nullptr
   );
   ShowWindow(g_hWnd, SW_SHOW);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE prevInst, LPSTR cmdArgs, int cmdShow)
{
   InitializeWindow(hInst, 800, 600);
   InitializeD3D();
   InitializeD2D();
   InitializeD3DTriangle();

   // Run message loop
   while (true)
   {
      // Handle windows messages
      MSG msg{};
      PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT)
         break;

      // Quit is escape is pressed
      if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
         break;

      // Do frame
      Update();
   }

   return 0;
}
