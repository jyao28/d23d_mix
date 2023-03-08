#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

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

//#define STB_IMAGE_IMPLEMENTATION
//#include "stb_image.h"

void AssertHResult(HRESULT hr, std::string&& errorMsg);

class d2d1_engine;

class d3d11_engine
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

   // Get methods
   auto get_texture2d()->ID3D11Texture2D* { return texture; }



   // Create D3D11 Device and Context
   UINT32 create_device_and_context();

   // Set up debug layer to break on D3D11 errors
   void setup_debug_layer();

   // Create Swap Chain
   void create_swap_chain(HWND hwnd);

   // Create render target view
   void create_render_target_view();

   // Create Vertex Shader
   HRESULT create_vertex_shader();

   // Create Pixel Shader
   HRESULT create_pixel_shader();

   // Create Input Layout
   void create_input_layout();

   // Create Vertex Buffer
   void create_vertex_buffer();

   // Create Sampler State
   void create_sampler_state();

   // Load Image
   ID3D11Texture2D* load_image(std::string image_file);


   void create_texture2d(std::string image_file, D3D11_USAGE usage, UINT bind_flags, UINT misc_flags);

   void create_texture2d()
   {
      create_texture2d("testTexture.png", D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0);
   }




   virtual void draw(D3D11_VIEWPORT& viewport);

   void resize();

   void present()
   {
      swap_chain->Present(1, 0);
   }

   void init(HWND hwnd);

   void update_image(d2d1_engine& d2d);

};

////////////////////////////////////////////////////////////////////////////////

class d2d1_engine
{
private:
   d3d11_engine d3d_coinst;

public:

   void init(HWND hwnd);

   auto get_texture2d() -> ID3D11Texture2D* { return d3d_coinst.get_texture2d(); }

};

