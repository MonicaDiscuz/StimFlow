#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace sf {

// 一张已上传到显存的 RGBA8 纹理（可选 mip 链）。
struct Texture {
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

Texture CreateTextureFromRgba(ID3D11Device* device, ID3D11DeviceContext* context,
                              const std::vector<std::uint8_t>& pixels, std::uint32_t width,
                              std::uint32_t height, bool generate_mips);

}  // namespace sf
