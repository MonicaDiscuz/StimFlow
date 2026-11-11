#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace sf {

// 12 个采样，启动时重采样成 5 档速度，用 PlaySound 播放。
// WAV 以 RCDATA 资源嵌入 exe，运行时不再依赖 assets/audio。
class ClickPlayer {
 public:
  ClickPlayer() = default;
  ~ClickPlayer();

  ClickPlayer(const ClickPlayer&) = delete;
  ClickPlayer& operator=(const ClickPlayer&) = delete;

  void LoadEmbedded();

  // pages_moved：一次翻了几页；velocity：拖拽释放速度（px/s）。
  void Play(int pages_moved, float velocity_px_per_s);

  void ToggleMute();
  [[nodiscard]] bool Muted() const noexcept { return muted_; }

  void Stop();

 private:
  static constexpr int kVariants = 5;

  struct Sample {
    std::array<std::vector<std::uint8_t>, kVariants> wav;
  };

  std::vector<Sample> samples_;
  std::mt19937 rng_{std::random_device{}()};
  std::uint64_t last_play_ms_ = 0;
  bool muted_ = false;
};

}  // namespace sf
