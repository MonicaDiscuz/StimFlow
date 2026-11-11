#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/Texture.h"
#include "image/WicDecoder.h"

namespace sf {

class FolderLibrary;
class ThumbnailCache;

// 通用图片 Cover Flow。缩略图按需从原图解码，不再使用预烘焙纹理。
class Renderer {
 public:
  explicit Renderer(HWND window, FolderLibrary* library, ThumbnailCache* cache = nullptr);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void Resize(std::uint32_t width, std::uint32_t height);
  void Render();

  [[nodiscard]] int CoverCount() const;

  [[nodiscard]] float Position() const noexcept { return position_; }
  [[nodiscard]] float CardWidthPx() const noexcept { return slot_width_; }
  [[nodiscard]] bool IsAnimating() const noexcept { return animating_; }

  void Step(int direction);
  void AnimateTo(int target);
  void SetPosition(float position);
  bool Tick();

  [[nodiscard]] bool InspectActive() const noexcept {
    return inspect_mode_ != InspectMode::None;
  }
  [[nodiscard]] bool InspectZoomed() const noexcept {
    return inspect_mode_ == InspectMode::Zoom;
  }
  [[nodiscard]] bool InspectHintActive() const noexcept;
  bool BeginInspect();
  void EndInspect();
  void RotateInspect(float dx, float dy);
  void ResetInspectRotation();
  void ZoomInspect(int wheel_delta);
  void ResetInspectZoom();
  void PanInspect(float dx, float dy);
  [[nodiscard]] bool CenterCardContains(int x, int y) const;

 private:
  enum class InspectMode { None, Rotate, Zoom };

  struct TextureEntry {
    Texture texture;
    bool high_res = false;
  };

  struct CardView {
    const Texture* texture = nullptr;
    float delta = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    float scale = 1.0f;
    float opacity = 1.0f;
    float width = 0.0f;
    float height = 0.0f;
  };

  void CreateDevice();
  void CreateSwapChain(HWND window);
  void CreateBackBuffer();
  void CreatePipeline();
  void CreateQuad();

  void EnsureWindow(int center);
  void EnsureWindowCircular(int from, int to);
  TextureEntry LoadEntry(int index, bool high_res);

  void DrawBackground();
  void DrawScene();
  void DrawShadow(const CardView& card, float scale, float blur, float alpha, float offset_y);
  void DrawReflection(const CardView& card);
  void DrawCard(const CardView& card);
  void UpdateTitle();
  void DrawTitle();
  void DrawInspect();
  void DrawTextQuad(const Texture& texture, float center_y, float height_px, float max_width,
                    float red, float green, float blue, float opacity);
  void ReleaseInspectFull();
  void UnbindTexture0();
  void TrimDevice();
  void FinishInspectZoomOut();

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> back_buffer_view_;

  Microsoft::WRL::ComPtr<ID3D11VertexShader> background_vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> background_pixel_shader_;

  Microsoft::WRL::ComPtr<ID3D11VertexShader> card_vertex_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> card_pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> reflection_pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> shadow_pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> text_pixel_shader_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> inspect_pixel_shader_;

  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> background_buffer_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
  Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;

  FolderLibrary* library_ = nullptr;
  ThumbnailCache* cache_ = nullptr;
  WicDecoder wic_;
  std::unordered_map<int, TextureEntry> textures_;
  Texture title_texture_;
  std::wstring title_text_;

  InspectMode inspect_mode_ = InspectMode::None;
  int inspect_index_ = 0;
  float inspect_yaw_ = 0.0f;
  float inspect_pitch_ = 0.0f;
  float inspect_zoom_ = 1.0f;
  float inspect_zoom_target_ = 1.0f;
  float inspect_pan_x_ = 0.0f;
  float inspect_pan_y_ = 0.0f;
  float inspect_base_w_ = 0.0f;
  float inspect_base_h_ = 0.0f;
  Texture inspect_base_;
  Texture inspect_hint_;
  std::uint32_t inspect_full_w_ = 0;
  std::uint32_t inspect_full_h_ = 0;
  std::vector<std::uint8_t> inspect_full_pixels_;
  std::unordered_map<int, Texture> inspect_tiles_;
  ULONGLONG inspect_hint_start_ = 0;
  ULONGLONG inspect_hint_until_ = 0;

  int center_index_ = 0;
  int title_index_ = 0;
  float position_ = 0.0f;
  float anim_target_position_ = 0.0f;
  float scroll_velocity_ = 0.0f;
  bool animating_ = false;

  float slot_width_ = 0.0f;
  float card_height_ = 0.0f;

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
};

}  // namespace sf
