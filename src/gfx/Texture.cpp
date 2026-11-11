#include "gfx/Texture.h"

#include "gfx/Hr.h"

#include <stdexcept>

namespace sf {

Texture CreateTextureFromRgba(ID3D11Device* device, ID3D11DeviceContext* context,
                              const std::vector<std::uint8_t>& pixels, std::uint32_t width,
                              std::uint32_t height, bool generate_mips) {
  if (width == 0 || height == 0 ||
      pixels.size() < static_cast<std::size_t>(width) * height * 4) {
    throw std::runtime_error("RGBA 纹理数据尺寸不匹配");
  }

  D3D11_TEXTURE2D_DESC description{};
  description.Width = width;
  description.Height = height;
  description.MipLevels = generate_mips ? 0 : 1;
  description.ArraySize = 1;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  if (generate_mips) {
    description.BindFlags |= D3D11_BIND_RENDER_TARGET;
    description.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  if (generate_mips) {
    Check(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf()),
          "ID3D11Device::CreateTexture2D（RGBA）");
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
  } else {
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * 4;
    Check(device->CreateTexture2D(&description, &initial, texture.GetAddressOf()),
          "ID3D11Device::CreateTexture2D（RGBA）");
  }

  D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
  view_description.Format = description.Format;
  view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view_description.Texture2D.MipLevels = generate_mips ? static_cast<UINT>(-1) : 1;

  Texture result;
  Check(device->CreateShaderResourceView(texture.Get(), &view_description,
                                         result.view.GetAddressOf()),
        "ID3D11Device::CreateShaderResourceView（RGBA）");
  if (generate_mips && context != nullptr) {
    context->GenerateMips(result.view.Get());
  }
  result.width = width;
  result.height = height;
  return result;
}

}  // namespace sf
