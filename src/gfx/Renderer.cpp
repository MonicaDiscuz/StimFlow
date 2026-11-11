#include "gfx/Renderer.h"

#include "cache/ThumbnailCache.h"
#include "gfx/Hr.h"
#include "gfx/Mat4.h"
#include "gfx/TextRenderer.h"
#include "image/FolderLibrary.h"

#include "kBackgroundPS.h"
#include "kBackgroundVS.h"
#include "kCardPS.h"
#include "kCardVS.h"
#include "kInspectPS.h"
#include "kReflectionPS.h"
#include "kShadowPS.h"
#include "kTextPS.h"

#include <d3d11sdklayers.h>
#include <dxgi1_3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace sf {
namespace {

constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr float kBackgroundRadius = 0.95f;

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
};

// ---- 布局参数 ----------------------------------------------------------------

constexpr int kSlotHalfRange = 8;  // 纹理缓存窗口 ±8，给动画留余量
constexpr float kVisibleHalfRange = 7.5f;

// Cover Flow 布局：统一对角线 + 统一底边。
constexpr float kCardDiagonalRatio = 0.60f;
constexpr float kCardBottomRatio = 0.72f;
constexpr float kMaxCardWidthRatio = 0.50f;
constexpr float kPageTurnRadians = 0.14f;  // 过渡中轻微 Y 轴翻转
constexpr float kScrollSpring = 160.0f;
constexpr float kScrollDamping = 24.0f;
constexpr float kScrollMaxLead = 5.0f;

// 标题样式。
constexpr float kTitleFontRatio = 0.027f;
constexpr float kTitleBottomGapLines = 2.0f;
constexpr float kTitleGray = 0.74f;
constexpr float kHintFadeMs = 200.0f;
constexpr float kInspectHintMs = 2000.0f;

constexpr float kRotationRadians = 47.0f * 3.14159265358979323846f / 180.0f;
constexpr float kCenterScale = 1.05f;
constexpr float kNeighborScale = 0.90f;
constexpr float kScaleStep = 0.08f;
constexpr float kScaleFloor = 0.58f;
constexpr float kOpacityStep = 0.05f;
constexpr float kOpacityFloor = 0.55f;
constexpr float kFocalCardWidths = 3.1f;
constexpr float kEdgeFadePixels = 80.0f;

// ---- 视觉效果参数 -----------------------------------------------------------

constexpr float kReflectionFraction = 0.42f;
constexpr float kReflectionAlpha = 0.34f;

constexpr float kShadowAmbientScale = 1.10f;
constexpr float kShadowAmbientBlur = 24.0f;
constexpr float kShadowAmbientAlpha = 0.35f;
constexpr float kShadowAmbientOffsetY = -10.0f;

constexpr float kShadowContactScale = 1.02f;
constexpr float kShadowContactBlur = 6.0f;
constexpr float kShadowContactAlpha = 0.50f;
constexpr float kShadowContactOffsetY = -4.0f;

constexpr float kCornerRadiusRatio = 0.04f;

struct Vertex {
  float x;
  float y;
  float u;
  float v;
};

struct SceneConstants {
  float transform[16];
  float viewport[4];
  float opacityParams[4];
  float cardParams[4];
  float shadowParams[4];
};
static_assert(sizeof(SceneConstants) == 128, "常量缓冲必须是 16 字节的倍数");

struct BackgroundConstants {
  float resolution[2];
  float center[2];
  float radius;
  float aspect;
  float padding[2];
};
static_assert(sizeof(BackgroundConstants) == 32, "常量缓冲必须是 16 字节的倍数");

constexpr Vertex kQuadVertices[] = {
    {-0.5f, 0.5f, 0.0f, 0.0f},
    {0.5f, 0.5f, 1.0f, 0.0f},
    {-0.5f, -0.5f, 0.0f, 1.0f},
    {0.5f, -0.5f, 1.0f, 1.0f},
};

UINT DeviceCreationFlags() {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  return flags;
}

void FillCommon(SceneConstants& constants, float viewport_width, float viewport_height,
                float opacity, float card_width, float card_height) {
  constants.viewport[0] = viewport_width * 0.5f;
  constants.viewport[1] = viewport_height * 0.5f;
  constants.viewport[2] = kEdgeFadePixels;
  constants.opacityParams[0] = opacity;
  constants.cardParams[0] = card_width;
  constants.cardParams[1] = card_height;
  constants.cardParams[2] = kReflectionFraction;
  constants.cardParams[3] = kReflectionAlpha;
}

void UploadConstants(ID3D11DeviceContext* context, ID3D11Buffer* buffer,
                     const SceneConstants& constants) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Check(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
        "ID3D11DeviceContext::Map");
  std::memcpy(mapped.pData, &constants, sizeof(constants));
  context->Unmap(buffer, 0);
}

int Mod(int value, int modulus) {
  int result = value % modulus;
  if (result < 0) {
    result += modulus;
  }
  return result;
}

int WrappedDeltaInt(int index, int center, int count) {
  int delta = index - center;
  delta %= count;
  if (delta < 0) {
    delta += count;
  }
  if (delta > count / 2) {
    delta -= count;
  }
  return delta;
}

// 把 index 与浮点 position 的差值折到 [-count/2, count/2)，用于首尾环绕。
float WrappedDelta(int index, float position, int count) {
  const float n = static_cast<float>(count);
  float delta = static_cast<float>(index) - position;
  delta = std::fmod(delta + n * 0.5f, n);
  if (delta < 0.0f) {
    delta += n;
  }
  return delta - n * 0.5f;
}

}  // namespace

Renderer::Renderer(HWND window, FolderLibrary* library, ThumbnailCache* cache)
    : library_(library), cache_(cache) {
  CreateDevice();
  CreateSwapChain(window);
  CreateBackBuffer();
  CreatePipeline();
  CreateQuad();

  card_height_ = static_cast<float>(height_) * 0.5f;
  slot_width_ = card_height_ * (2.0f / 3.0f);

  if (CoverCount() > 0) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, CoverCount() - 1);
    center_index_ = dist(rng);
    title_index_ = center_index_;
    position_ = static_cast<float>(center_index_);
    anim_target_position_ = position_;
    EnsureWindow(center_index_);
  }

#ifdef _DEBUG
  wchar_t message[256];
  swprintf_s(message, L"StimFlow: %d images, %d cached\n", CoverCount(),
             static_cast<int>(textures_.size()));
  OutputDebugStringW(message);
#endif
}

int Renderer::CoverCount() const {
  return library_ != nullptr ? static_cast<int>(library_->Count()) : 0;
}

Renderer::~Renderer() {
  if (context_) {
    context_->ClearState();
    context_->Flush();
  }

  textures_.clear();
  title_texture_.view.Reset();
  inspect_tiles_.clear();
  inspect_full_pixels_.clear();
  inspect_base_.view.Reset();
  inspect_hint_.view.Reset();

  blend_state_.Reset();
  sampler_.Reset();
  rasterizer_.Reset();
  background_buffer_.Reset();
  constant_buffer_.Reset();
  vertex_buffer_.Reset();
  input_layout_.Reset();
  text_pixel_shader_.Reset();
  inspect_pixel_shader_.Reset();
  shadow_pixel_shader_.Reset();
  reflection_pixel_shader_.Reset();
  card_pixel_shader_.Reset();
  card_vertex_shader_.Reset();
  background_pixel_shader_.Reset();
  background_vertex_shader_.Reset();

  back_buffer_view_.Reset();
  swap_chain_.Reset();

#ifdef _DEBUG
  if (device_) {
    Microsoft::WRL::ComPtr<ID3D11Debug> d3d_debug;
    if (SUCCEEDED(device_.As(&d3d_debug))) {
      d3d_debug->ReportLiveDeviceObjects(static_cast<D3D11_RLDO_FLAGS>(
          D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
    }
  }
#endif

  context_.Reset();
  factory_.Reset();
  device_.Reset();
}

void Renderer::CreateDevice() {
  UINT flags = DeviceCreationFlags();
  D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kFeatureLevels,
      static_cast<UINT>(std::size(kFeatureLevels)), D3D11_SDK_VERSION,
      device_.GetAddressOf(), &obtained, context_.GetAddressOf());

#ifdef _DEBUG
  if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
    flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kFeatureLevels,
                           static_cast<UINT>(std::size(kFeatureLevels)), D3D11_SDK_VERSION,
                           device_.GetAddressOf(), &obtained, context_.GetAddressOf());
  }
#endif

  Check(hr, "D3D11CreateDevice");

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  Check(device_.As(&dxgi_device), "QueryInterface(IDXGIDevice)");

  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  Check(dxgi_device->GetAdapter(adapter.GetAddressOf()), "IDXGIDevice::GetAdapter");

  Check(adapter->GetParent(IID_PPV_ARGS(factory_.GetAddressOf())), "IDXGIAdapter::GetParent");
}

void Renderer::CreateSwapChain(HWND window) {
  RECT client{};
  CheckWin32(GetClientRect(window, &client), "GetClientRect");
  width_ = static_cast<std::uint32_t>(client.right - client.left);
  height_ = static_cast<std::uint32_t>(client.bottom - client.top);

  DXGI_SWAP_CHAIN_DESC1 description{};
  description.Width = width_;
  description.Height = height_;
  description.Format = kBackBufferFormat;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = 2;
  description.Scaling = DXGI_SCALING_STRETCH;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

  Check(factory_->CreateSwapChainForHwnd(device_.Get(), window, &description, nullptr, nullptr,
                                         swap_chain_.GetAddressOf()),
        "CreateSwapChainForHwnd");

  Check(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
        "IDXGIFactory::MakeWindowAssociation");
}

void Renderer::CreateBackBuffer() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  Check(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer");
  Check(device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                        back_buffer_view_.GetAddressOf()),
        "CreateRenderTargetView");
}

void Renderer::CreatePipeline() {
  Check(device_->CreateVertexShader(kBackgroundVS, sizeof(kBackgroundVS), nullptr,
                                    background_vertex_shader_.GetAddressOf()),
        "CreateVertexShader（背景）");
  Check(device_->CreatePixelShader(kBackgroundPS, sizeof(kBackgroundPS), nullptr,
                                   background_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（背景）");

  Check(device_->CreateVertexShader(kCardVS, sizeof(kCardVS), nullptr,
                                    card_vertex_shader_.GetAddressOf()),
        "CreateVertexShader（卡片）");
  Check(device_->CreatePixelShader(kCardPS, sizeof(kCardPS), nullptr,
                                   card_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（卡片）");
  Check(device_->CreatePixelShader(kReflectionPS, sizeof(kReflectionPS), nullptr,
                                   reflection_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（倒影）");
  Check(device_->CreatePixelShader(kShadowPS, sizeof(kShadowPS), nullptr,
                                   shadow_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（阴影）");
  Check(device_->CreatePixelShader(kTextPS, sizeof(kTextPS), nullptr,
                                   text_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（标题）");
  Check(device_->CreatePixelShader(kInspectPS, sizeof(kInspectPS), nullptr,
                                   inspect_pixel_shader_.GetAddressOf()),
        "CreatePixelShader（查看）");

  const D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, x)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, u)),
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  Check(device_->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)), kCardVS,
                                   sizeof(kCardVS), input_layout_.GetAddressOf()),
        "CreateInputLayout");

  D3D11_BUFFER_DESC constants_description{};
  constants_description.ByteWidth = static_cast<UINT>(sizeof(SceneConstants));
  constants_description.Usage = D3D11_USAGE_DYNAMIC;
  constants_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  constants_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  Check(device_->CreateBuffer(&constants_description, nullptr,
                              constant_buffer_.GetAddressOf()),
        "CreateBuffer（场景常量）");

  D3D11_BUFFER_DESC background_description{};
  background_description.ByteWidth = static_cast<UINT>(sizeof(BackgroundConstants));
  background_description.Usage = D3D11_USAGE_DYNAMIC;
  background_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  background_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  Check(device_->CreateBuffer(&background_description, nullptr,
                              background_buffer_.GetAddressOf()),
        "CreateBuffer（背景常量）");

  D3D11_SAMPLER_DESC sampler_description{};
  sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_description.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
  Check(device_->CreateSamplerState(&sampler_description, sampler_.GetAddressOf()),
        "CreateSamplerState");

  D3D11_RASTERIZER_DESC rasterizer_description{};
  rasterizer_description.FillMode = D3D11_FILL_SOLID;
  rasterizer_description.CullMode = D3D11_CULL_NONE;
  rasterizer_description.FrontCounterClockwise = FALSE;
  rasterizer_description.DepthClipEnable = TRUE;
  Check(device_->CreateRasterizerState(&rasterizer_description, rasterizer_.GetAddressOf()),
        "CreateRasterizerState");

  D3D11_BLEND_DESC blend_description{};
  blend_description.RenderTarget[0].BlendEnable = TRUE;
  blend_description.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_description.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_description.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_description.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_description.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend_description.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_description.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  Check(device_->CreateBlendState(&blend_description, blend_state_.GetAddressOf()),
        "CreateBlendState");
}

void Renderer::CreateQuad() {
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = static_cast<UINT>(sizeof(kQuadVertices));
  description.Usage = D3D11_USAGE_IMMUTABLE;
  description.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = kQuadVertices;

  Check(device_->CreateBuffer(&description, &initial, vertex_buffer_.GetAddressOf()),
        "CreateBuffer（顶点缓冲）");
}

void Renderer::EnsureWindow(int center) { EnsureWindowCircular(center, center); }

void Renderer::EnsureWindowCircular(int from, int to) {
  if (CoverCount() == 0) {
    return;
  }

  const int count = CoverCount();
  const int lo = std::min(from, to) - kSlotHalfRange;
  const int hi = std::max(from, to) + kSlotHalfRange;

  std::vector<int> desired;
  desired.reserve(static_cast<std::size_t>(hi - lo + 1));
  for (int position = lo; position <= hi; ++position) {
    const int index = Mod(position, count);
    if (std::find(desired.begin(), desired.end(), index) == desired.end()) {
      desired.push_back(index);
    }
  }

  for (auto it = textures_.begin(); it != textures_.end();) {
    if (std::find(desired.begin(), desired.end(), it->first) == desired.end()) {
      it = textures_.erase(it);
    } else {
      ++it;
    }
  }

  for (int index : desired) {
    const bool high_res = std::abs(WrappedDeltaInt(index, center_index_, count)) <= 1;
    auto it = textures_.find(index);
    if (it == textures_.end()) {
      textures_.emplace(index, LoadEntry(index, high_res));
    } else if (it->second.high_res != high_res) {
      it->second = LoadEntry(index, high_res);
    }
  }
}

Renderer::TextureEntry Renderer::LoadEntry(int index, bool high_res) {
  TextureEntry entry;
  entry.high_res = high_res;
  if (library_ == nullptr || index < 0 || index >= CoverCount()) {
    return entry;
  }

  std::vector<std::uint8_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const std::uint32_t max_width = high_res ? 512u : 256u;
  const std::uint32_t max_height = high_res ? 768u : 384u;

  bool loaded = high_res && cache_ != nullptr &&
                cache_->Load(library_->At(index).path, pixels, width, height);
  if (!loaded &&
      wic_.Decode(library_->At(index).path, max_width, max_height, pixels, width, height)) {
    loaded = true;
    if (high_res && cache_ != nullptr) {
      cache_->Store(library_->At(index).path, pixels, width, height);
    }
  }
  if (loaded) {
    entry.texture = CreateTextureFromRgba(device_.Get(), context_.Get(), pixels, width, height,
                                          true);
  }
  return entry;
}

void Renderer::AnimateTo(int target) {
  if (CoverCount() == 0) {
    return;
  }

  const int count = CoverCount();
  if (target > static_cast<int>(std::floor(position_ + kScrollMaxLead))) {
    target = static_cast<int>(std::floor(position_ + kScrollMaxLead));
  } else if (target < static_cast<int>(std::ceil(position_ - kScrollMaxLead))) {
    target = static_cast<int>(std::ceil(position_ - kScrollMaxLead));
  }

  if (!animating_ && std::abs(position_ - static_cast<float>(target)) < 0.0005f) {
    return;
  }

  anim_target_position_ = static_cast<float>(target);
  animating_ = true;
  center_index_ = Mod(target, count);
  EnsureWindowCircular(static_cast<int>(std::lround(position_)), target);
}

void Renderer::Step(int direction) {
  if (CoverCount() == 0) {
    return;
  }
  const int current = static_cast<int>(
      std::lround(animating_ ? anim_target_position_ : position_));
  const int target = current + direction;
  if (direction > 0 && target > static_cast<int>(std::floor(position_ + kScrollMaxLead))) {
    return;
  }
  if (direction < 0 && target < static_cast<int>(std::ceil(position_ - kScrollMaxLead))) {
    return;
  }
  AnimateTo(target);
}

bool Renderer::Tick() {
  bool active = false;

  if (animating_) {
    constexpr float kTickSeconds = 1.0f / 60.0f;
    const float displacement = anim_target_position_ - position_;
    scroll_velocity_ += displacement * kScrollSpring * kTickSeconds;
    scroll_velocity_ *= std::exp(-kScrollDamping * kTickSeconds);
    position_ += scroll_velocity_ * kTickSeconds;

    if (std::abs(displacement) < 0.0008f && std::abs(scroll_velocity_) < 0.02f) {
      const int count = CoverCount();
      position_ = static_cast<float>(Mod(static_cast<int>(std::lround(anim_target_position_)), count));
      anim_target_position_ = position_;
      scroll_velocity_ = 0.0f;
      animating_ = false;
      center_index_ = static_cast<int>(position_);
      title_index_ = center_index_;
      EnsureWindow(center_index_);
      UpdateTitle();
    } else {
      active = true;
      const int count = CoverCount();
      const int display = Mod(static_cast<int>(std::lround(position_)), count);
      if (display != title_index_) {
        title_index_ = display;
        UpdateTitle();
      }
    }
  }

  if (inspect_mode_ != InspectMode::None) {
    const float difference = inspect_zoom_target_ - inspect_zoom_;
    if (std::abs(difference) > 0.0001f) {
      inspect_zoom_ += difference * 0.22f;
      if (std::abs(inspect_zoom_target_ - inspect_zoom_) < 0.0005f) {
        inspect_zoom_ = inspect_zoom_target_;
      }

      const float displayed_width = inspect_base_w_ * inspect_zoom_;
      const float displayed_height = inspect_base_h_ * inspect_zoom_;
      const float max_x =
          std::max(0.0f, (displayed_width - static_cast<float>(width_)) * 0.5f);
      const float max_y =
          std::max(0.0f, (displayed_height - static_cast<float>(height_)) * 0.5f);
      inspect_pan_x_ = std::clamp(inspect_pan_x_, -max_x, max_x);
      inspect_pan_y_ = std::clamp(inspect_pan_y_, -max_y, max_y);
      active = true;
    }

    if (inspect_mode_ == InspectMode::Zoom && inspect_zoom_target_ <= 1.0f &&
        inspect_zoom_ <= 1.001f) {
      FinishInspectZoomOut();
      active = animating_;
    }
  }

  return active;
}

void Renderer::SetPosition(float position) {
  if (CoverCount() == 0) {
    return;
  }

  animating_ = false;
  scroll_velocity_ = 0.0f;
  position_ = position;
  anim_target_position_ = position;

  const int count = CoverCount();
  const int display = Mod(static_cast<int>(std::lround(position_)), count);
  title_index_ = display;
  if (display != center_index_) {
    center_index_ = display;
    EnsureWindow(display);
  }
}

void Renderer::DrawBackground() {
  BackgroundConstants constants{};
  constants.resolution[0] = static_cast<float>(width_);
  constants.resolution[1] = static_cast<float>(height_);
  constants.center[0] = 0.5f;
  constants.center[1] = 0.5f;
  constants.radius = kBackgroundRadius;
  constants.aspect = static_cast<float>(width_) / static_cast<float>(height_);

  D3D11_MAPPED_SUBRESOURCE mapped{};
  Check(context_->Map(background_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
        "ID3D11DeviceContext::Map（背景）");
  std::memcpy(mapped.pData, &constants, sizeof(constants));
  context_->Unmap(background_buffer_.Get(), 0);

  ID3D11Buffer* buffer = background_buffer_.Get();
  context_->PSSetConstantBuffers(1, 1, &buffer);

  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(background_vertex_shader_.Get(), nullptr, 0);
  context_->PSSetShader(background_pixel_shader_.Get(), nullptr, 0);
  context_->RSSetState(rasterizer_.Get());
  context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  context_->Draw(3, 0);
}

void Renderer::DrawShadow(const CardView& card, float scale, float blur, float alpha,
                          float offset_y) {
  const float card_pixel_width = card.width;
  const float card_pixel_height = card.height;
  const float focal = kFocalCardWidths * slot_width_;

  Mat4 transform = Scaling(card_pixel_width * scale, card_pixel_height * scale, 1.0f);
  transform = Multiply(transform, RotationY(card.angle));
  transform = Multiply(transform, Translation(card.x, card.y + offset_y, 0.0f));
  transform = Multiply(transform,
                       CardProjection(static_cast<float>(width_), static_cast<float>(height_),
                                      focal));

  SceneConstants constants{};
  std::memcpy(constants.transform, transform.m, sizeof(transform.m));
  FillCommon(constants, static_cast<float>(width_), static_cast<float>(height_), card.opacity,
             card_pixel_width, card_pixel_height);
  constants.shadowParams[0] = scale;
  constants.shadowParams[1] = blur;
  constants.shadowParams[2] = kCornerRadiusRatio * card_pixel_width;
  constants.shadowParams[3] = alpha;

  UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
  context_->PSSetShader(shadow_pixel_shader_.Get(), nullptr, 0);
  context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
}

void Renderer::DrawReflection(const CardView& card) {
  const float card_pixel_width = card.width;
  const float card_pixel_height = card.height;
  const float reflection_height = card_pixel_height * kReflectionFraction;
  const float center_y = -(card_pixel_height * 0.5f + reflection_height * 0.5f);
  const float focal = kFocalCardWidths * slot_width_;

  Mat4 transform = Scaling(card_pixel_width, reflection_height, 1.0f);
  transform = Multiply(transform, RotationY(card.angle));
  transform = Multiply(transform, Translation(card.x, card.y + center_y, 0.0f));
  transform = Multiply(transform,
                       CardProjection(static_cast<float>(width_), static_cast<float>(height_),
                                      focal));

  SceneConstants constants{};
  std::memcpy(constants.transform, transform.m, sizeof(transform.m));
  FillCommon(constants, static_cast<float>(width_), static_cast<float>(height_), card.opacity,
             card_pixel_width, card_pixel_height);

  UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
  context_->PSSetShader(reflection_pixel_shader_.Get(), nullptr, 0);
  if (card.texture != nullptr) {
    ID3D11ShaderResourceView* view = card.texture->view.Get();
    context_->PSSetShaderResources(0, 1, &view);
  }
  context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
}

void Renderer::DrawCard(const CardView& card) {
  const float card_pixel_width = card.width;
  const float card_pixel_height = card.height;
  const float focal = kFocalCardWidths * slot_width_;

  Mat4 transform = Scaling(card_pixel_width, card_pixel_height, 1.0f);
  transform = Multiply(transform, RotationY(card.angle));
  transform = Multiply(transform, Translation(card.x, card.y, 0.0f));
  transform = Multiply(transform,
                       CardProjection(static_cast<float>(width_), static_cast<float>(height_),
                                      focal));

  SceneConstants constants{};
  std::memcpy(constants.transform, transform.m, sizeof(transform.m));
  FillCommon(constants, static_cast<float>(width_), static_cast<float>(height_), card.opacity,
             card_pixel_width, card_pixel_height);
  constants.opacityParams[1] = card.delta;

  UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
  context_->PSSetShader(card_pixel_shader_.Get(), nullptr, 0);
  if (card.texture != nullptr) {
    ID3D11ShaderResourceView* view = card.texture->view.Get();
    context_->PSSetShaderResources(0, 1, &view);
  }
  context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
}

void Renderer::UpdateTitle() {
  if (library_ == nullptr || title_index_ < 0 || title_index_ >= CoverCount()) {
    return;
  }

  const std::wstring& title = library_->At(title_index_).title;
  if (title == title_text_ && (title_texture_.view || title.empty())) {
    return;
  }

  title_text_ = title;
  title_texture_ = {};
  if (title.empty()) {
    return;
  }

  const auto font_px =
      static_cast<std::uint32_t>(std::max(20.0f, static_cast<float>(height_) * kTitleFontRatio));
  const auto max_width =
      static_cast<std::uint32_t>(static_cast<float>(width_) * 0.60f);
  title_texture_ = CreateTextTexture(device_.Get(), context_.Get(), title, font_px, max_width);
}

void Renderer::DrawTitle() {
  if (!title_texture_.view || width_ == 0 || height_ == 0) {
    return;
  }

  const float baseline = static_cast<float>(height_) * kCardBottomRatio;
  const float font_px = std::max(18.0f, static_cast<float>(height_) * kTitleFontRatio);
  const float text_height = font_px * 1.4f;
  float center_y = baseline + kTitleBottomGapLines * text_height + text_height * 0.5f;
  const float bottom = static_cast<float>(height_) - text_height * 0.5f - 8.0f;
  if (center_y > bottom) {
    center_y = bottom;
  }

  DrawTextQuad(title_texture_, center_y, text_height, static_cast<float>(width_) * 0.60f,
               kTitleGray, kTitleGray, kTitleGray, 1.0f);
}

void Renderer::DrawTextQuad(const Texture& texture, float center_y, float height_px,
                            float max_width, float red, float green, float blue,
                            float opacity) {
  if (!texture.view || width_ == 0 || height_ == 0) {
    return;
  }

  float text_height = height_px;
  float text_width =
      text_height * (static_cast<float>(texture.width) / static_cast<float>(texture.height));
  if (text_width > max_width) {
    text_width = max_width;
    text_height = text_width * texture.height / texture.width;
  }

  const float ndc_y = 1.0f - 2.0f * center_y / static_cast<float>(height_);
  const float scale_x = 2.0f * text_width / static_cast<float>(width_);
  const float scale_y = 2.0f * text_height / static_cast<float>(height_);

  Mat4 transform = Scaling(scale_x, scale_y, 1.0f);
  transform = Multiply(transform, Translation(0.0f, ndc_y, 0.0f));

  SceneConstants constants{};
  std::memcpy(constants.transform, transform.m, sizeof(transform.m));
  constants.viewport[0] = static_cast<float>(width_) * 0.5f;
  constants.viewport[1] = static_cast<float>(height_) * 0.5f;
  constants.viewport[2] = kEdgeFadePixels;
  constants.opacityParams[0] = opacity;
  constants.opacityParams[1] = red;
  constants.opacityParams[2] = green;
  constants.opacityParams[3] = blue;

  UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
  context_->PSSetShader(text_pixel_shader_.Get(), nullptr, 0);
  ID3D11ShaderResourceView* view = texture.view.Get();
  context_->PSSetShaderResources(0, 1, &view);
  context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
}

bool Renderer::CenterCardContains(int x, int y) const {
  if (InspectActive() || CoverCount() == 0) {
    return false;
  }

  const int count = CoverCount();
  const int display_center = Mod(static_cast<int>(std::lround(position_)), count);
  const auto it = textures_.find(display_center);
  if (it == textures_.end() || it->second.texture.view == nullptr) {
    return false;
  }

  const float aspect = static_cast<float>(it->second.texture.width) /
                       static_cast<float>(it->second.texture.height);
  const float target_diagonal =
      static_cast<float>(std::min(width_, height_)) * kCardDiagonalRatio;
  float height = (target_diagonal / std::sqrt(1.0f + aspect * aspect)) * kCenterScale;
  float width = height * aspect;
  const float max_width = static_cast<float>(width_) * kMaxCardWidthRatio;
  if (width > max_width) {
    width = max_width;
    height = width / aspect;
  }

  const float baseline = static_cast<float>(height_) * kCardBottomRatio;
  const float center_x = static_cast<float>(width_) * 0.5f;
  const float center_y = baseline - height * 0.5f;
  return std::abs(static_cast<float>(x) - center_x) <= width * 0.5f &&
         std::abs(static_cast<float>(y) - center_y) <= height * 0.5f;
}

bool Renderer::BeginInspect() {
  if (InspectActive() || CoverCount() == 0) {
    return false;
  }

  const int count = CoverCount();
  const int display_center = Mod(static_cast<int>(std::lround(position_)), count);
  const auto entry = textures_.find(display_center);
  if (entry == textures_.end() || entry->second.texture.view == nullptr) {
    return false;
  }

  std::vector<std::uint8_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (!wic_.Decode(library_->At(display_center).path, 2048, 2048, pixels, width, height)) {
    return false;
  }

  inspect_base_ = CreateTextureFromRgba(device_.Get(), context_.Get(), pixels, width, height, true);
  const float aspect = static_cast<float>(width) / static_cast<float>(height);
  const float min_dimension = static_cast<float>(std::min(width_, height_));
  float display_height = (min_dimension * 0.70f) / std::sqrt(1.0f + aspect * aspect);
  float display_width = display_height * aspect;
  const float max_width = static_cast<float>(width_) * 0.90f;
  const float max_height = static_cast<float>(height_) * 0.90f;
  if (display_width > max_width) {
    display_width = max_width;
    display_height = display_width / aspect;
  }
  if (display_height > max_height) {
    display_height = max_height;
    display_width = display_height * aspect;
  }

  inspect_base_w_ = display_width;
  inspect_base_h_ = display_height;
  inspect_index_ = display_center;
  inspect_yaw_ = 0.0f;
  inspect_pitch_ = 0.0f;
  inspect_zoom_ = 1.0f;
  inspect_zoom_target_ = 1.0f;
  inspect_pan_x_ = 0.0f;
  inspect_pan_y_ = 0.0f;
  inspect_mode_ = InspectMode::Rotate;

  const auto hint_font = static_cast<std::uint32_t>(
      std::max(18.0f, static_cast<float>(height_) * 0.024f));
  inspect_hint_ = CreateTextTexture(device_.Get(), context_.Get(),
                                    L"旋转 拖动/左摇杆 · 缩放 滚轮/LT·RT · 重置 双击/X · 返回 Esc/B",
                                    hint_font,
                                    static_cast<std::uint32_t>(static_cast<float>(width_) * 0.70f));
  inspect_hint_start_ = GetTickCount64();
  inspect_hint_until_ = inspect_hint_start_ + 2000;
  return true;
}

void Renderer::EndInspect() {
  inspect_mode_ = InspectMode::None;
  UnbindTexture0();
  inspect_tiles_.clear();
  ReleaseInspectFull();
  inspect_base_.view.Reset();
  inspect_hint_.view.Reset();
  inspect_zoom_ = 1.0f;
  inspect_zoom_target_ = 1.0f;
  inspect_pan_x_ = 0.0f;
  inspect_pan_y_ = 0.0f;
  inspect_yaw_ = 0.0f;
  inspect_pitch_ = 0.0f;
  TrimDevice();
}

bool Renderer::InspectHintActive() const noexcept {
  return inspect_mode_ != InspectMode::None && inspect_hint_.view != nullptr &&
         GetTickCount64() < inspect_hint_until_;
}

void Renderer::ReleaseInspectFull() {
  // clear() 只销毁元素，不释放容量；这里显式归还整块原图缓冲。
  std::vector<std::uint8_t>().swap(inspect_full_pixels_);
  inspect_full_w_ = 0;
  inspect_full_h_ = 0;
}

void Renderer::UnbindTexture0() {
  if (context_ != nullptr) {
    ID3D11ShaderResourceView* null_view = nullptr;
    context_->PSSetShaderResources(0, 1, &null_view);
  }
}

void Renderer::TrimDevice() {
  if (context_ != nullptr) {
    context_->ClearState();
    context_->Flush();
  }
  Microsoft::WRL::ComPtr<IDXGIDevice3> device3;
  if (SUCCEEDED(device_.As(&device3)) && device3 != nullptr) {
    // 让驱动归还已释放的纹理/分块占用的显存分配。
    device3->Trim();
  }
}

void Renderer::RotateInspect(float dx, float dy) {
  if (inspect_mode_ != InspectMode::Rotate) {
    return;
  }
  inspect_yaw_ += dx * 0.006f;
  inspect_pitch_ = std::clamp(inspect_pitch_ + dy * 0.006f, -1.48f, 1.48f);
}

void Renderer::ResetInspectRotation() {
  if (inspect_mode_ == InspectMode::Rotate) {
    inspect_yaw_ = 0.0f;
    inspect_pitch_ = 0.0f;
  }
}

void Renderer::ZoomInspect(int wheel_delta) {
  if (!InspectActive() || wheel_delta == 0) {
    return;
  }
  if (inspect_mode_ == InspectMode::Rotate && wheel_delta < 0) {
    return;  // 1× 再往下滚不做任何事。
  }

  const float factor = std::pow(1.1f, static_cast<float>(wheel_delta) / 120.0f);
  const float new_target = std::clamp(inspect_zoom_target_ * factor, 1.0f, 4.0f);

  if (new_target <= 1.0f) {
    inspect_zoom_target_ = 1.0f;
    if (inspect_zoom_ <= 1.001f) {
      FinishInspectZoomOut();
    }
    return;
  }

  if (inspect_full_pixels_.empty()) {
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!wic_.Decode(library_->At(inspect_index_).path, 8192, 8192, pixels, width, height)) {
      return;
    }
    inspect_full_w_ = width;
    inspect_full_h_ = height;
    inspect_full_pixels_ = std::move(pixels);
  }

  inspect_zoom_target_ = new_target;
  if (inspect_mode_ != InspectMode::Zoom) {
    inspect_mode_ = InspectMode::Zoom;
  }

  const float displayed_width = inspect_base_w_ * inspect_zoom_;
  const float displayed_height = inspect_base_h_ * inspect_zoom_;
  const float max_x = std::max(0.0f, (displayed_width - static_cast<float>(width_)) * 0.5f);
  const float max_y = std::max(0.0f, (displayed_height - static_cast<float>(height_)) * 0.5f);
  inspect_pan_x_ = std::clamp(inspect_pan_x_, -max_x, max_x);
  inspect_pan_y_ = std::clamp(inspect_pan_y_, -max_y, max_y);
}

void Renderer::ResetInspectZoom() {
  if (!InspectActive()) {
    return;
  }
  inspect_zoom_target_ = 1.0f;
  if (inspect_zoom_ <= 1.001f) {
    FinishInspectZoomOut();
  }
}

void Renderer::FinishInspectZoomOut() {
  inspect_zoom_ = 1.0f;
  inspect_zoom_target_ = 1.0f;
  inspect_mode_ = InspectMode::Rotate;
  inspect_pan_x_ = 0.0f;
  inspect_pan_y_ = 0.0f;
  UnbindTexture0();
  inspect_tiles_.clear();
  ReleaseInspectFull();
  TrimDevice();
}

void Renderer::PanInspect(float dx, float dy) {
  if (inspect_mode_ != InspectMode::Zoom) {
    return;
  }

  const float displayed_width = inspect_base_w_ * inspect_zoom_;
  const float displayed_height = inspect_base_h_ * inspect_zoom_;
  const float max_x = std::max(0.0f, (displayed_width - static_cast<float>(width_)) * 0.5f);
  const float max_y = std::max(0.0f, (displayed_height - static_cast<float>(height_)) * 0.5f);
  inspect_pan_x_ = std::clamp(inspect_pan_x_ + dx, -max_x, max_x);
  inspect_pan_y_ = std::clamp(inspect_pan_y_ + dy, -max_y, max_y);
}

void Renderer::DrawInspect() {
  if (!inspect_base_.view) {
    return;
  }

  ID3D11Buffer* constant_buffer = constant_buffer_.Get();
  context_->VSSetConstantBuffers(0, 1, &constant_buffer);
  context_->PSSetConstantBuffers(0, 1, &constant_buffer);

  const UINT stride = static_cast<UINT>(sizeof(Vertex));
  const UINT offset = 0;
  ID3D11Buffer* vertex_buffer = vertex_buffer_.Get();
  context_->IASetInputLayout(input_layout_.Get());
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
  context_->VSSetShader(card_vertex_shader_.Get(), nullptr, 0);
  ID3D11SamplerState* sampler = sampler_.Get();
  context_->PSSetSamplers(0, 1, &sampler);
  context_->RSSetState(rasterizer_.Get());
  context_->OMSetBlendState(blend_state_.Get(), nullptr, 0xffffffff);
  context_->PSSetShader(inspect_pixel_shader_.Get(), nullptr, 0);

  if (inspect_mode_ == InspectMode::Rotate) {
    const float focal = 3.0f * std::max(inspect_base_w_, inspect_base_h_);
    Mat4 transform = Scaling(inspect_base_w_, inspect_base_h_, 1.0f);
    transform = Multiply(transform, RotationX(inspect_pitch_));
    transform = Multiply(transform, RotationY(inspect_yaw_));
    transform = Multiply(transform,
                         CardProjection(static_cast<float>(width_), static_cast<float>(height_),
                                        focal));
    SceneConstants constants{};
    std::memcpy(constants.transform, transform.m, sizeof(transform.m));
    constants.opacityParams[0] = 1.0f;
    UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
    ID3D11ShaderResourceView* view = inspect_base_.view.Get();
    context_->PSSetShaderResources(0, 1, &view);
    context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
    return;
  }

  if (inspect_full_pixels_.empty() || inspect_full_w_ == 0 || inspect_full_h_ == 0) {
    return;
  }

  constexpr int kTile = 512;
  const int columns = (static_cast<int>(inspect_full_w_) + kTile - 1) / kTile;
  const int rows = (static_cast<int>(inspect_full_h_) + kTile - 1) / kTile;
  const float scale =
      (inspect_base_w_ * inspect_zoom_) / static_cast<float>(inspect_full_w_);
  if (scale <= 0.0f) {
    return;
  }

  const float half_view_w = static_cast<float>(width_) * 0.5f / scale;
  const float half_view_h = static_cast<float>(height_) * 0.5f / scale;
  const float center_x = static_cast<float>(inspect_full_w_) * 0.5f - inspect_pan_x_ / scale;
  const float center_y = static_cast<float>(inspect_full_h_) * 0.5f - inspect_pan_y_ / scale;

  const int first_x = std::max(0, static_cast<int>((center_x - half_view_w) / kTile));
  const int last_x = std::min(columns - 1, static_cast<int>((center_x + half_view_w) / kTile));
  const int first_y = std::max(0, static_cast<int>((center_y - half_view_h) / kTile));
  const int last_y = std::min(rows - 1, static_cast<int>((center_y + half_view_h) / kTile));

  std::vector<int> visible;
  for (int ty = first_y; ty <= last_y; ++ty) {
    for (int tx = first_x; tx <= last_x; ++tx) {
      const int key = ty * columns + tx;
      auto tile = inspect_tiles_.find(key);
      if (tile == inspect_tiles_.end()) {
        const int tile_x = tx * kTile;
        const int tile_y = ty * kTile;
        const int tile_w = std::min(kTile, static_cast<int>(inspect_full_w_) - tile_x);
        const int tile_h = std::min(kTile, static_cast<int>(inspect_full_h_) - tile_y);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(tile_w) * tile_h * 4);
        for (int row = 0; row < tile_h; ++row) {
          const std::uint8_t* source =
              inspect_full_pixels_.data() +
              (static_cast<std::size_t>(tile_y + row) * inspect_full_w_ + tile_x) * 4;
          std::memcpy(pixels.data() + static_cast<std::size_t>(row) * tile_w * 4, source,
                      static_cast<std::size_t>(tile_w) * 4);
        }
        Texture texture = CreateTextureFromRgba(device_.Get(), context_.Get(), pixels, tile_w,
                                                tile_h, false);
        tile = inspect_tiles_.emplace(key, std::move(texture)).first;
      }

      const int tile_x = tx * kTile;
      const int tile_y = ty * kTile;
      const int tile_w = std::min(kTile, static_cast<int>(inspect_full_w_) - tile_x);
      const int tile_h = std::min(kTile, static_cast<int>(inspect_full_h_) - tile_y);
      const float screen_x =
          (static_cast<float>(tile_x) + tile_w * 0.5f - inspect_full_w_ * 0.5f) * scale +
          inspect_pan_x_;
      const float screen_y =
          (static_cast<float>(tile_y) + tile_h * 0.5f - inspect_full_h_ * 0.5f) * scale +
          inspect_pan_y_;
      const float screen_w = tile_w * scale;
      const float screen_h = tile_h * scale;

      Mat4 transform = Scaling(2.0f * screen_w / static_cast<float>(width_),
                               2.0f * screen_h / static_cast<float>(height_), 1.0f);
      transform = Multiply(transform,
                           Translation(2.0f * screen_x / static_cast<float>(width_),
                                       -2.0f * screen_y / static_cast<float>(height_), 0.0f));
      SceneConstants constants{};
      std::memcpy(constants.transform, transform.m, sizeof(transform.m));
      constants.opacityParams[0] = 1.0f;
      UploadConstants(context_.Get(), constant_buffer_.Get(), constants);
      ID3D11ShaderResourceView* view = tile->second.view.Get();
      context_->PSSetShaderResources(0, 1, &view);
      context_->Draw(static_cast<UINT>(std::size(kQuadVertices)), 0);
      visible.push_back(key);
    }
  }

  for (auto tile = inspect_tiles_.begin(); tile != inspect_tiles_.end();) {
    if (std::find(visible.begin(), visible.end(), tile->first) == visible.end()) {
      tile = inspect_tiles_.erase(tile);
    } else {
      ++tile;
    }
  }
}

void Renderer::DrawScene() {
  if (textures_.empty() || CoverCount() == 0) {
    return;
  }

  UpdateTitle();

  std::array<CardView, 17> by_offset{};
  std::array<bool, 17> valid{};

  const int cover_count = CoverCount();
  const int base = static_cast<int>(std::lround(position_));
  const float max_width = static_cast<float>(width_) * kMaxCardWidthRatio;
  const float overlap = 0.18f * slot_width_;
  const float target_diagonal =
      static_cast<float>(std::min(width_, height_)) * kCardDiagonalRatio;
  const float baseline = static_cast<float>(height_) * kCardBottomRatio;

  for (int offset = -8; offset <= 8; ++offset) {
    const int index = Mod(base + offset, cover_count);
    const float delta = WrappedDelta(index, position_, cover_count);
    const float abs_delta = std::abs(delta);
    if (abs_delta > kVisibleHalfRange) {
      continue;
    }

    const auto it = textures_.find(index);
    if (it == textures_.end() || it->second.texture.view == nullptr) {
      continue;
    }

    const float sign = delta < 0.0f ? -1.0f : 1.0f;
    CardView view;
    view.texture = &it->second.texture;
    view.delta = delta;

    if (abs_delta <= 1.0f) {
      view.scale = kCenterScale + (kNeighborScale - kCenterScale) * abs_delta;
      view.opacity = 1.0f - kOpacityStep * abs_delta;
      view.angle = sign * kRotationRadians * abs_delta;
    } else {
      view.scale = std::max(kNeighborScale - kScaleStep * (abs_delta - 1.0f), kScaleFloor);
      view.opacity = std::max(1.0f - kOpacityStep * abs_delta, kOpacityFloor);
      view.angle = sign * kRotationRadians;
    }

    const float aspect = static_cast<float>(it->second.texture.width) /
                         static_cast<float>(it->second.texture.height);
    float height =
        (target_diagonal / std::sqrt(1.0f + aspect * aspect)) * view.scale;
    float width = height * aspect;
    if (width > max_width) {
      width = max_width;
      height = width / aspect;
    }
    view.width = width;
    view.height = height;
    const float focal = kFocalCardWidths * slot_width_;
    const float sin_angle = std::abs(std::sin(view.angle));
    const float corner_scale =
        std::max(0.45f, 1.0f - (width * 0.5f * sin_angle) / focal);
    view.y = height * 0.5f +
             (static_cast<float>(height_) * 0.5f - baseline) * corner_scale;

    // 翻页过渡：靠近中心的卡片在两张图之间轻微绕 Y 轴翻转。
    const float fraction = position_ - std::floor(position_);
    const float turn = std::sin(fraction * 3.14159265358979323846f) * kPageTurnRadians;
    if (abs_delta < 1.5f) {
      const float weight = std::max(0.0f, 1.0f - abs_delta);
      view.angle += turn * (delta < 0.0f ? 1.0f : -1.0f) * weight;
    }

    by_offset[offset + 8] = view;
    valid[offset + 8] = true;
  }

  // 按实际投影宽度累加 x，宽图不会挤在一起。
  if (valid[8]) {
    by_offset[8].x = 0.0f;
  }
  for (int distance = 1; distance <= 8; ++distance) {
    const int right = 8 + distance;
    if (valid[right]) {
      const bool has_prev = valid[right - 1];
      const CardView* prev = has_prev ? &by_offset[right - 1] : nullptr;
      const float prev_half = prev != nullptr
                                  ? 0.5f * prev->width * std::cos(prev->angle)
                                  : 0.5f * slot_width_ * std::cos(kRotationRadians);
      const float prev_x = prev != nullptr ? prev->x : 0.0f;
      const float current_half =
          0.5f * by_offset[right].width * std::cos(by_offset[right].angle);
      by_offset[right].x = prev_x + prev_half + current_half - overlap;
    }

    const int left = 8 - distance;
    if (valid[left]) {
      const bool has_prev = valid[left + 1];
      const CardView* prev = has_prev ? &by_offset[left + 1] : nullptr;
      const float prev_half = prev != nullptr
                                  ? 0.5f * prev->width * std::cos(prev->angle)
                                  : 0.5f * slot_width_ * std::cos(kRotationRadians);
      const float prev_x = prev != nullptr ? prev->x : 0.0f;
      const float current_half =
          0.5f * by_offset[left].width * std::cos(by_offset[left].angle);
      by_offset[left].x = prev_x - prev_half - current_half + overlap;
    }
  }

  std::array<CardView, 32> views{};
  int view_count = 0;
  for (int offset = -8; offset <= 8; ++offset) {
    if (valid[offset + 8]) {
      views[view_count++] = by_offset[offset + 8];
    }
  }

  // 按 |delta| 降序：远处先画，中心最后画。
  for (int i = 1; i < view_count; ++i) {
    const CardView key = views[i];
    int j = i - 1;
    while (j >= 0 && std::abs(views[j].delta) < std::abs(key.delta)) {
      views[j + 1] = views[j];
      --j;
    }
    views[j + 1] = key;
  }

  ID3D11Buffer* constant_buffer = constant_buffer_.Get();
  context_->VSSetConstantBuffers(0, 1, &constant_buffer);
  context_->PSSetConstantBuffers(0, 1, &constant_buffer);

  const UINT stride = static_cast<UINT>(sizeof(Vertex));
  const UINT offset = 0;
  ID3D11Buffer* vertex_buffer = vertex_buffer_.Get();
  context_->IASetInputLayout(input_layout_.Get());
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);

  context_->VSSetShader(card_vertex_shader_.Get(), nullptr, 0);
  ID3D11SamplerState* sampler = sampler_.Get();
  context_->PSSetSamplers(0, 1, &sampler);
  context_->RSSetState(rasterizer_.Get());
  context_->OMSetBlendState(blend_state_.Get(), nullptr, 0xffffffff);

  for (int i = 0; i < view_count; ++i) {
    DrawShadow(views[i], kShadowAmbientScale, kShadowAmbientBlur, kShadowAmbientAlpha,
               kShadowAmbientOffsetY);
    DrawShadow(views[i], kShadowContactScale, kShadowContactBlur, kShadowContactAlpha,
               kShadowContactOffsetY);
    DrawReflection(views[i]);
    DrawCard(views[i]);
  }

  DrawTitle();

  context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  ID3D11ShaderResourceView* null_view = nullptr;
  context_->PSSetShaderResources(0, 1, &null_view);
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
  if (!swap_chain_ || width == 0 || height == 0 || (width == width_ && height == height_)) {
    return;
  }

  context_->OMSetRenderTargets(0, nullptr, nullptr);
  back_buffer_view_.Reset();

  Check(swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
        "IDXGISwapChain::ResizeBuffers");

  width_ = width;
  height_ = height;
  CreateBackBuffer();

  card_height_ = static_cast<float>(height_) * 0.5f;
  slot_width_ = card_height_ * (2.0f / 3.0f);
  title_text_.clear();
}

void Renderer::Render() {
  if (!back_buffer_view_) {
    return;
  }

  ID3D11RenderTargetView* views[] = {back_buffer_view_.Get()};
  context_->OMSetRenderTargets(1, views, nullptr);

  const D3D11_VIEWPORT viewport{
      0.0f,
      0.0f,
      static_cast<float>(width_),
      static_cast<float>(height_),
      0.0f,
      1.0f,
  };
  context_->RSSetViewports(1, &viewport);
  context_->ClearRenderTargetView(back_buffer_view_.Get(), kClearColor);

  DrawBackground();

  if (InspectActive()) {
    DrawInspect();

    const ULONGLONG now = GetTickCount64();
    if (inspect_hint_.view && now < inspect_hint_until_) {
      const float age = static_cast<float>(now - inspect_hint_start_);
      float alpha = 1.0f;
      if (age < kHintFadeMs) {
        alpha = age / kHintFadeMs;
      } else if (static_cast<float>(inspect_hint_until_ - now) < kHintFadeMs) {
        alpha = static_cast<float>(inspect_hint_until_ - now) / kHintFadeMs;
      }
      alpha = std::clamp(alpha, 0.0f, 1.0f);
      if (alpha > 0.001f) {
        const float hint_font = std::max(18.0f, static_cast<float>(height_) * 0.024f);
        DrawTextQuad(inspect_hint_, static_cast<float>(height_) * 0.065f, hint_font * 1.4f,
                     static_cast<float>(width_) * 0.70f, 1.0f, 1.0f, 1.0f, alpha);
      }
    }
  } else {
    DrawScene();
  }

  const HRESULT hr = swap_chain_->Present(1, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    Check(device_->GetDeviceRemovedReason(), "Present（设备已移除）");
  }
  Check(hr, "IDXGISwapChain::Present");
}

}  // namespace sf
