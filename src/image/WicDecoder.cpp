#include "image/WicDecoder.h"

#include "gfx/Hr.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sf {
namespace {

WICBitmapTransformOptions OrientationTransform(UINT orientation) {
  switch (orientation) {
    case 2:
      return WICBitmapTransformFlipHorizontal;
    case 3:
      return WICBitmapTransformRotate180;
    case 4:
      return WICBitmapTransformFlipVertical;
    case 5:
      return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 |
                                                    WICBitmapTransformFlipHorizontal);
    case 6:
      return WICBitmapTransformRotate90;
    case 7:
      return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 |
                                                    WICBitmapTransformFlipHorizontal);
    case 8:
      return WICBitmapTransformRotate270;
    default:
      return WICBitmapTransformRotate0;
  }
}

UINT ReadOrientation(IWICBitmapFrameDecode* frame) {
  Microsoft::WRL::ComPtr<IWICMetadataQueryReader> reader;
  if (FAILED(frame->GetMetadataQueryReader(reader.GetAddressOf()))) {
    return 1;
  }

  PROPVARIANT value;
  PropVariantInit(&value);
  UINT orientation = 1;
  const wchar_t* paths[] = {
      L"/app1/ifd/{ushort=274}",
      L"/app1/ifd/exif/{ushort=274}",
  };
  for (const wchar_t* path : paths) {
    if (SUCCEEDED(reader->GetMetadataByName(path, &value))) {
      if (value.vt == VT_UI2) {
        orientation = value.uiVal;
      } else if (value.vt == VT_UI4) {
        orientation = value.ulVal;
      }
      break;
    }
  }
  PropVariantClear(&value);
  return orientation;
}

}  // namespace

WicDecoder::WicDecoder() {
  Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(factory_.GetAddressOf())),
        "CoCreateInstance(WICImagingFactory)");
}

WicDecoder::~WicDecoder() = default;

bool WicDecoder::Decode(const std::filesystem::path& path, std::uint32_t max_width,
                        std::uint32_t max_height, std::vector<std::uint8_t>& pixels,
                        std::uint32_t& width, std::uint32_t& height) {
  if (!factory_) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand,
                                                 decoder.GetAddressOf()))) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
    return false;
  }

  const UINT orientation = ReadOrientation(frame.Get());

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory_->CreateFormatConverter(converter.GetAddressOf())) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapSource> source;
  if (FAILED(converter.As(&source))) {
    return false;
  }

  if (orientation != 1) {
    Microsoft::WRL::ComPtr<IWICBitmapFlipRotator> rotator;
    if (FAILED(factory_->CreateBitmapFlipRotator(rotator.GetAddressOf())) ||
        FAILED(rotator->Initialize(source.Get(), OrientationTransform(orientation))) ||
        FAILED(rotator.As(&source))) {
      return false;
    }
  }

  UINT source_width = 0;
  UINT source_height = 0;
  if (FAILED(source->GetSize(&source_width, &source_height)) || source_width == 0 ||
      source_height == 0) {
    return false;
  }

  UINT target_width = source_width;
  UINT target_height = source_height;
  if (max_width > 0 || max_height > 0) {
    float scale = 1.0f;
    if (max_width > 0) {
      scale = std::min(scale, static_cast<float>(max_width) / static_cast<float>(source_width));
    }
    if (max_height > 0) {
      scale = std::min(scale, static_cast<float>(max_height) / static_cast<float>(source_height));
    }
    if (scale < 1.0f) {
      target_width = std::max(1u, static_cast<UINT>(std::lround(source_width * scale)));
      target_height = std::max(1u, static_cast<UINT>(std::lround(source_height * scale)));
    }
  }

  if (target_width != source_width || target_height != source_height) {
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory_->CreateBitmapScaler(scaler.GetAddressOf())) ||
        FAILED(scaler->Initialize(source.Get(), target_width, target_height,
                                  WICBitmapInterpolationModeFant)) ||
        FAILED(scaler.As(&source))) {
      return false;
    }
  }

  const UINT stride = target_width * 4;
  pixels.resize(static_cast<std::size_t>(stride) * target_height);
  if (FAILED(source->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()),
                                pixels.data()))) {
    pixels.clear();
    return false;
  }

  // WIC 原生 32bpp 是 BGRA；渲染器统一使用 RGBA，这里交换红蓝通道。
  // 注意：缩放器对 RGBA 的处理在不同 Windows 版本上不一致，统一走 BGRA 更稳。
  for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
    std::swap(pixels[i], pixels[i + 2]);
  }

  width = target_width;
  height = target_height;
  return true;
}

}  // namespace sf
