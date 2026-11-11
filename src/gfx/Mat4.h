#pragma once

#include <cmath>

namespace sf {

// 行主序 4x4 矩阵，配合 HLSL 的 `mul(float4, matrix)`（行向量约定）。
// 只实现这一步需要的四个变换，不引入 DirectXMath。
struct Mat4 {
  float m[16] = {};
};

inline Mat4 Identity() {
  Mat4 result;
  result.m[0] = 1.0f;
  result.m[5] = 1.0f;
  result.m[10] = 1.0f;
  result.m[15] = 1.0f;
  return result;
}

// 行主序乘法：result = a * b。
inline Mat4 Multiply(const Mat4& a, const Mat4& b) {
  Mat4 result;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a.m[row * 4 + k] * b.m[k * 4 + column];
      }
      result.m[row * 4 + column] = sum;
    }
  }
  return result;
}

inline Mat4 Scaling(float x, float y, float z) {
  Mat4 result = Identity();
  result.m[0] = x;
  result.m[5] = y;
  result.m[10] = z;
  return result;
}

inline Mat4 Translation(float x, float y, float z) {
  Mat4 result = Identity();
  result.m[12] = x;
  result.m[13] = y;
  result.m[14] = z;
  return result;
}

// 绕 Y 轴旋转。正角度把 +X 边转向 -Z（靠近相机），用于左侧卡片。
inline Mat4 RotationY(float radians) {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  Mat4 result = Identity();
  result.m[0] = cosine;
  result.m[2] = -sine;
  result.m[8] = sine;
  result.m[10] = cosine;
  return result;
}

// 绕 X 轴旋转。正角度把 +Y 边转向 +Z（远离相机）。
inline Mat4 RotationX(float radians) {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  Mat4 result = Identity();
  result.m[5] = cosine;
  result.m[6] = sine;
  result.m[9] = -sine;
  result.m[10] = cosine;
  return result;
}

// 把「像素单位」的卡片投影到 NDC：
//   x_ndc = (2 * x / screen_w) / (1 + z / focal)
//   y_ndc = (2 * y / screen_h) / (1 + z / focal)
//   z_ndc = 0.5（没有深度缓冲，固定值即可）
inline Mat4 CardProjection(float screen_width, float screen_height, float focal) {
  Mat4 result;
  result.m[0] = 2.0f / screen_width;
  result.m[5] = 2.0f / screen_height;
  // 行向量约定：w 由第 3 列决定（m[11]），z_ndc 由第 4 行决定（m[14]）。
  result.m[11] = 1.0f / focal;  // w = 1 + z / focal
  result.m[14] = 0.5f;          // z_ndc = 0.5
  result.m[15] = 1.0f;
  return result;
}

}  // namespace sf
