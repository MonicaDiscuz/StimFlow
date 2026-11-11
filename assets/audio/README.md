# 音效采样素材

来源：`machine.mp3`（Pixabay Content License）
<https://pixabay.com/service/license-summary/>

## 内容

| 文件 | 内容 |
|---|---|
| `click_01.wav` … `click_12.wav` | 从参考音频截取的 12 个单次 click |
| `samples.json` | 每个样本的来源时间、时长、峰值 |

- 格式：24 kHz / 16-bit / stereo
- 长度：每个 100 ms
- 峰值：−6 dBFS
- 首尾已做 1.5 ms / 5 ms 淡入淡出

## 提取规则

从参考音频中挑选了 12 个相对独立、没有被下一次 click 覆盖的位置；每个样本从峰值前 3 ms 开始截取 100 ms，归一化后保存。

## 使用方式

构建时以 PCM 形式嵌入 exe；运行时由 `PlaySound` 从内存播放。12 个样本循环 / 随机取用，保留原素材的自然差异。

这些 WAV 只用于 StimFlow 内的音效，不单独分发。替换素材时请确认新的授权。
