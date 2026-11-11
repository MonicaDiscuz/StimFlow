// StimFlow 全部 HLSL。
// 构建时由 fxc 编译成字节码头文件，运行时不再需要 d3dcompiler_47.dll。

// ---------------------------------------------------------------------------
// 背景
// ---------------------------------------------------------------------------

struct BackgroundOutput {
    float4 position : SV_POSITION;
};

BackgroundOutput BackgroundVS(uint vertex_id : SV_VERTEXID) {
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    BackgroundOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

cbuffer BackgroundConstants : register(b1) {
    float2 resolution;
    float2 center;
    float radius;
    float aspect;
    float2 padding;
};

float Hash12(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float4 BackgroundPS(float4 position : SV_POSITION) : SV_TARGET {
    float2 uv = position.xy / resolution;
    float2 delta = (uv - center) * float2(aspect, 1.0);
    float t = saturate(length(delta) / max(radius, 0.0001));
    t = t * t * (3.0 - 2.0 * t);

    // 中心 #101014 → 边缘 #000000。
    float3 center_color = float3(16.0, 16.0, 20.0) / 255.0;
    float3 edge_color = float3(0.0, 0.0, 0.0);
    float3 color = lerp(center_color, edge_color, t);

    // ±1/255 抖动，防止 8-bit 渐变出现色带。
    color += (Hash12(position.xy) - 0.5) / 255.0;
    return float4(color, 1.0);
}

// ---------------------------------------------------------------------------
// 卡片
// ---------------------------------------------------------------------------

cbuffer SceneConstants : register(b0) {
    row_major float4x4 transform;
    float4 viewport;
    float4 opacityParams;
    float4 cardParams;
    float4 shadowParams;
};

struct VSInput {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 0.0, 1.0), transform);
    output.uv = input.uv;
    return output;
}

Texture2D coverTexture : register(t0);
SamplerState coverSampler : register(s0);

float4 CardPS(PSInput input) : SV_TARGET {
    float4 color = coverTexture.Sample(coverSampler, input.uv);

    // 1 px 屏幕空间边框，12% 白；远卡衰减，避免斜边锯齿过亮。
    float2 uv_per_pixel = max(fwidth(input.uv), 1e-6);
    float2 edge = min(input.uv, 1.0 - input.uv) / uv_per_pixel;
    float border = 1.0 - smoothstep(0.0, 1.5, min(edge.x, edge.y));
    float border_strength = 0.12 * saturate(1.0 - abs(opacityParams.y) * 0.2);
    color.rgb = lerp(color.rgb, 1.0, border * border_strength);

    float distance_from_center = abs(input.position.x - viewport.x);
    float fade = saturate((viewport.x - distance_from_center) / max(viewport.z, 1.0));
    fade *= fade;

    return float4(color.rgb, color.a * opacityParams.x * fade);
}

float4 ReflectionPS(PSInput input) : SV_TARGET {
    // 倒影几何的高度 = cardParams.z × 卡高；uv.y 0 = 靠近卡片，1 = 最远端。
    float t = saturate(input.uv.y);
    float sample_v = 1.0 - t * cardParams.z;

    // 用 mip 采样代替独立模糊 pass：越远越模糊。
    // 清晰版：alpha 0.34, mip 1.0–4.5, RGB ×0.85；旧版：0.26, 2.0–5.5, ×0.72。
    float mip = lerp(1.0, 4.5, t);
    float4 color = coverTexture.SampleLevel(coverSampler, float2(input.uv.x, sample_v), mip);
    color.rgb *= 0.85;  // 地面反射比原图暗

    // alpha 从 0.34 线性衰减到 0。
    float alpha = (1.0 - t) * cardParams.w;

    float distance_from_center = abs(input.position.x - viewport.x);
    float fade = saturate((viewport.x - distance_from_center) / max(viewport.z, 1.0));
    fade *= fade;

    return float4(color.rgb, color.a * alpha * opacityParams.x * fade);
}

float RoundedBoxSDF(float2 p, float2 half_size, float radius) {
    float2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 ShadowPS(PSInput input) : SV_TARGET {
    float2 half_size = cardParams.xy * 0.5;
    float2 shadow_half = half_size * shadowParams.x;
    float2 p = (input.uv - 0.5) * 2.0 * shadow_half;

    float radius = min(shadowParams.z, min(half_size.x, half_size.y));
    float sdf = RoundedBoxSDF(p, half_size, radius);
    float blur = max(shadowParams.y, 0.001);
    float alpha = saturate(0.5 - sdf / (2.0 * blur));

    float distance_from_center = abs(input.position.x - viewport.x);
    float fade = saturate((viewport.x - distance_from_center) / max(viewport.z, 1.0));
    fade *= fade;

    return float4(0.0, 0.0, 0.0, alpha * shadowParams.w * opacityParams.x * fade);
}

// 标题：GDI 生成的纹理，RGB 全白，alpha = 字形亮度。
Texture2D textTexture : register(t0);
SamplerState textSampler : register(s0);

float4 TextPS(PSInput input) : SV_TARGET {
    float4 color = textTexture.Sample(textSampler, input.uv);
    return float4(opacityParams.yzw, color.a * opacityParams.x);
}

// 查看模式：不画边框、不做边缘淡出。
float4 InspectPS(PSInput input) : SV_TARGET {
    return coverTexture.Sample(coverSampler, input.uv);
}
