#include "audio/ClickPlayer.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace sf {
namespace {

struct WavData {
  int channels = 0;
  int sample_rate = 0;
  std::vector<std::int16_t> samples;
};

std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool ParseWav(const std::uint8_t* bytes, std::size_t size, WavData& out) {
  if (size < 12) {
    return false;
  }

  const auto tag = [&](std::size_t offset) {
    return std::string(reinterpret_cast<const char*>(bytes + offset), 4);
  };
  if (tag(0) != "RIFF" || tag(8) != "WAVE") {
    return false;
  }

  int format = 0;
  int bits_per_sample = 0;
  std::size_t data_offset = 0;
  std::size_t data_size = 0;
  bool found_data = false;

  for (std::size_t offset = 12; offset + 8 <= size;) {
    const std::string id = tag(offset);
    const std::uint32_t chunk_size = ReadU32(bytes, offset + 4);
    const std::size_t body = offset + 8;
    if (body + chunk_size > size) {
      break;
    }

    if (id == "fmt " && chunk_size >= 16) {
      format = ReadU16(bytes, body);
      out.channels = ReadU16(bytes, body + 2);
      out.sample_rate = static_cast<int>(ReadU32(bytes, body + 4));
      bits_per_sample = ReadU16(bytes, body + 14);
    } else if (id == "data") {
      data_offset = body;
      data_size = chunk_size;
      found_data = true;
    }

    offset = body + chunk_size + (chunk_size & 1u);
  }

  if (format != 1 || bits_per_sample != 16 || out.channels < 1 || out.channels > 2 ||
      out.sample_rate <= 0 || !found_data || data_size < 2) {
    return false;
  }

  const std::size_t sample_count = data_size / 2;
  out.samples.resize(sample_count);
  for (std::size_t i = 0; i < sample_count; ++i) {
    out.samples[i] = static_cast<std::int16_t>(ReadU16(bytes, data_offset + i * 2));
  }
  return true;
}

std::vector<std::int16_t> Resample(const std::vector<std::int16_t>& input, int channels,
                                   float speed) {
  const std::size_t frames = input.size() / static_cast<std::size_t>(channels);
  if (frames < 2 || speed <= 0.0f) {
    return {};
  }

  const std::size_t output_frames =
      std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<float>(frames) / speed));
  std::vector<std::int16_t> output(output_frames * static_cast<std::size_t>(channels));

  for (std::size_t i = 0; i < output_frames; ++i) {
    const double source = static_cast<double>(i) * speed;
    const std::size_t first = std::min(static_cast<std::size_t>(source), frames - 1);
    const std::size_t second = std::min(first + 1, frames - 1);
    const double fraction = source - static_cast<double>(first);

    for (int channel = 0; channel < channels; ++channel) {
      const double a = input[first * channels + channel];
      const double b = input[second * channels + channel];
      output[i * channels + channel] =
          static_cast<std::int16_t>(std::lround(a + (b - a) * fraction));
    }
  }
  return output;
}

std::vector<std::uint8_t> BuildWav(const std::vector<std::int16_t>& pcm, int channels,
                                   int sample_rate) {
  const std::uint32_t data_bytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
  const std::uint32_t riff_size = 36 + data_bytes;
  const std::uint16_t block_align = static_cast<std::uint16_t>(channels * 2);
  const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * block_align;

  std::vector<std::uint8_t> wav(44 + data_bytes);
  const auto put16 = [&](std::size_t offset, std::uint16_t value) {
    wav[offset] = static_cast<std::uint8_t>(value & 0xFF);
    wav[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  };
  const auto put32 = [&](std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      wav[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
  };

  std::memcpy(wav.data(), "RIFF", 4);
  put32(4, riff_size);
  std::memcpy(wav.data() + 8, "WAVE", 4);
  std::memcpy(wav.data() + 12, "fmt ", 4);
  put32(16, 16);
  put16(20, 1);
  put16(22, static_cast<std::uint16_t>(channels));
  put32(24, static_cast<std::uint32_t>(sample_rate));
  put32(28, byte_rate);
  put16(32, block_align);
  put16(34, 16);
  std::memcpy(wav.data() + 36, "data", 4);
  put32(40, data_bytes);
  if (data_bytes > 0) {
    std::memcpy(wav.data() + 44, pcm.data(), data_bytes);
  }
  return wav;
}

}  // namespace

ClickPlayer::~ClickPlayer() { Stop(); }

void ClickPlayer::LoadEmbedded() {
  samples_.clear();
  constexpr float kSpeeds[kVariants] = {0.85f, 0.925f, 1.0f, 1.075f, 1.15f};

  for (int id = 1; id <= 12; ++id) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (resource == nullptr) {
      continue;
    }
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    if (loaded == nullptr) {
      continue;
    }
    const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded));
    const DWORD size = SizeofResource(nullptr, resource);
    if (data == nullptr || size < 44) {
      continue;
    }

    WavData wav;
    if (!ParseWav(data, size, wav)) {
      continue;
    }

    Sample sample;
    bool valid = true;
    for (int variant = 0; variant < kVariants; ++variant) {
      const std::vector<std::int16_t> pcm =
          Resample(wav.samples, wav.channels, kSpeeds[variant]);
      if (pcm.empty()) {
        valid = false;
        break;
      }
      sample.wav[variant] = BuildWav(pcm, wav.channels, wav.sample_rate);
    }

    if (valid) {
      samples_.push_back(std::move(sample));
    }
  }
}

void ClickPlayer::Play(int pages_moved, float velocity_px_per_s) {
  if (muted_ || samples_.empty()) {
    return;
  }

  const std::uint64_t now = GetTickCount64();
  if (now - last_play_ms_ < 50) {  // 最快 20 次/秒
    return;
  }
  last_play_ms_ = now;

  int variant = 2;  // 1.0×
  if (pages_moved >= 4 || velocity_px_per_s > 1200.0f) {
    variant = 4;  // 1.15×
  } else if (pages_moved >= 2 || velocity_px_per_s > 600.0f) {
    variant = 3;  // 1.075×
  } else if (pages_moved <= 0 && velocity_px_per_s < 200.0f) {
    variant = 1;  // 0.925×
  }

  std::uniform_int_distribution<std::size_t> pick(0, samples_.size() - 1);
  const std::vector<std::uint8_t>& wav = samples_[pick(rng_)].wav[variant];
  if (wav.empty()) {
    return;
  }

  PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void ClickPlayer::ToggleMute() {
  muted_ = !muted_;
  if (muted_) {
    Stop();
  }
}

void ClickPlayer::Stop() { PlaySoundW(nullptr, nullptr, 0); }

}  // namespace sf
