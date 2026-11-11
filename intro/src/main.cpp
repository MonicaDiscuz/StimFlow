#include <windows.h>
#include <d3d11.h>

#include "shader_vs.h"
#include "shader_ps.h"

extern "C" int _fltused = 0;

static volatile LONG g_run = 1;
static float g_target = 4.0f;
static float g_pos = 4.0f;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_KEYDOWN) {
    if (wp == VK_ESCAPE) {
      g_run = 0;
    } else if (wp == VK_LEFT) {
      g_target -= 1.0f;
    } else if (wp == VK_RIGHT) {
      g_target += 1.0f;
    }
  } else if (msg == WM_DESTROY) {
    g_run = 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

extern "C" void __cdecl Entry() {
  SetProcessDPIAware();

  HINSTANCE instance = GetModuleHandleA(0);
  static WNDCLASSA window_class;
  window_class.lpfnWndProc = WndProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = "s";
  RegisterClassA(&window_class);

  const int screen_w = GetSystemMetrics(0);
  const int screen_h = GetSystemMetrics(1);
  HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, "s", "STIMFLOW", WS_POPUP | WS_VISIBLE, 0, 0, screen_w,
                              screen_h, 0, 0, instance, 0);
  ShowCursor(0);
  SetForegroundWindow(hwnd);

  static DXGI_SWAP_CHAIN_DESC swap_desc;
  swap_desc.BufferCount = 1;
  swap_desc.BufferDesc.Width = screen_w;
  swap_desc.BufferDesc.Height = screen_h;
  swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.OutputWindow = hwnd;
  swap_desc.SampleDesc.Count = 1;
  swap_desc.Windowed = TRUE;
  swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  static D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
  };

  IDXGISwapChain* swap = 0;
  ID3D11Device* device = 0;
  ID3D11DeviceContext* context = 0;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, levels, 3, D3D11_SDK_VERSION, &swap_desc, &swap,
      &device, 0, &context);
  if (FAILED(hr)) {
    hr = D3D11CreateDeviceAndSwapChain(
        0, D3D_DRIVER_TYPE_WARP, 0, 0, levels, 3, D3D11_SDK_VERSION, &swap_desc, &swap,
        &device, 0, &context);
  }
  if (FAILED(hr)) {
    ExitProcess(1);
  }

  ID3D11Texture2D* back_buffer = 0;
  swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
  ID3D11RenderTargetView* target = 0;
  if (FAILED(device->CreateRenderTargetView(back_buffer, 0, &target))) {
    ExitProcess(4);
  }
  back_buffer->Release();

  ID3D11VertexShader* vs = 0;
  ID3D11PixelShader* ps = 0;
  if (FAILED(device->CreateVertexShader(g_vsCode, sizeof(g_vsCode), 0, &vs))) {
    ExitProcess(2);
  }
  if (FAILED(device->CreatePixelShader(g_psCode, sizeof(g_psCode), 0, &ps))) {
    ExitProcess(3);
  }

  static D3D11_BUFFER_DESC cb_desc;
  cb_desc.ByteWidth = 16;
  cb_desc.Usage = D3D11_USAGE_DYNAMIC;
  cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ID3D11Buffer* cb = 0;
  device->CreateBuffer(&cb_desc, 0, &cb);

  static D3D11_VIEWPORT viewport;
  viewport.Width = (float)screen_w;
  viewport.Height = (float)screen_h;
  viewport.MaxDepth = 1.0f;
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs, 0, 0);
  context->PSSetShader(ps, 0, 0);
  context->PSSetConstantBuffers(0, 1, &cb);
  context->OMSetRenderTargets(1, &target, 0);

  const DWORD start = GetTickCount();
  MSG msg;
  while (g_run) {
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        g_run = 0;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if (!g_run) {
      break;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    g_pos += (g_target - g_pos) * 0.18f;
    if (SUCCEEDED(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      float* params = (float*)mapped.pData;
      params[0] = (GetTickCount() - start) * 0.001f;
      params[1] = (float)screen_w;
      params[2] = (float)screen_h;
      params[3] = g_pos;
      context->Unmap(cb, 0);
    }

    context->Draw(3, 0);
    swap->Present(1, 0);
  }

  ExitProcess(0);
}
