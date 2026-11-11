cbuffer Params : register(b0) {
    float4 p;
};

struct VOut {
    float4 pos : SV_POSITION;
};

VOut VSMain(uint id : SV_VERTEXID) {
    VOut o;
    o.pos = float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0.0, 1.0);
    return o;
}

float3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (t + float3(0.0, 0.33, 0.67)));
}

static const uint FONT[56] = {
    14, 17, 16, 14,  1, 17, 14,   // S
    31,  4,  4,  4,  4,  4,  4,   // T
    14,  4,  4,  4,  4,  4, 14,   // I
    17, 27, 21, 21, 17, 17, 17,   // M
    31, 16, 16, 30, 16, 16, 16,   // F
    16, 16, 16, 16, 16, 16, 31,   // L
    14, 17, 17, 17, 17, 17, 14,   // O
    17, 17, 17, 21, 21, 27, 17    // W
};

float text_mask(float2 uv) {
    float2 t = (uv - float2(0.5, 0.09)) / float2(0.38, 0.05) + 0.5;
    if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) {
        return 0.0;
    }
    int ci = min(7, (int)(t.x * 8.0));
    int col = (int)(frac(t.x * 8.0) * 6.0);
    if (col > 4) {
        return 0.0;
    }
    int row = (int)(t.y * 7.0);
    uint bits = FONT[ci * 7 + row];
    return (float)((bits >> (4 - col)) & 1u);
}

float4 PSMain(VOut i) : SV_TARGET {
    float2 res = p.yz;
    float2 uv = i.pos.xy / res;
    float aspect = res.x / res.y;
    float2 q = (uv - 0.5) * float2(aspect, 1.0);
    float t = p.x;

    float3 col = lerp(float3(0.015, 0.015, 0.022), float3(0.0, 0.0, 0.0),
                      saturate(length(q) * 1.3));
    col += float3(0.08, 0.03, 0.12) * exp(-abs(q.y - 0.24) * 9.0) * 0.35;
    col += float3(0.10, 0.05, 0.16) * exp(-abs(q.y - 0.24) * 45.0) * 0.35;

    float2 grid = abs(frac(q * 14.0) - 0.5);
    col += smoothstep(0.47, 0.5, max(grid.x, grid.y)) * 0.012;

    float pos = p.w;
    float best = 99.0;

    [loop] for (int i = 0; i < 9; i++) {
        float d = (float)i - pos;
        d -= 9.0 * floor(d / 9.0 + 0.5);
        float ad = abs(d);
        float ang = d * 0.22;
        float s = 1.0 / (1.0 + ad * 0.14);
        float2 hs = float2(0.15, 0.20) * s;
        float2 c = float2(1.6 * sin(ang), 1.6 - 1.6 * cos(ang));
        float2 l = (q - c) / hs;

        float a = ang;
        float ca = cos(a);
        float sa = sin(a);
        float2 r = float2(l.x * ca - l.y * sa, l.x * sa + l.y * ca);

        float2 e = abs(r) - 0.92;
        float sdf = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0) - 0.08;
        float card = 1.0 - smoothstep(-0.03, 0.03, sdf);

        float rb = (q.y - (c.y + hs.y)) / hs.y;
        float rmask = saturate(1.0 - rb * 4.0) * smoothstep(0.02, 0.08, rb) *
                      (1.0 - smoothstep(0.0, 0.08, abs(r.x) - 0.92)) * 0.22;

        float3 base = palette(frac((float)i * 0.13 + 0.55));
        float shade = 0.55 + 0.45 * s;
        float2 art_uv = r * 2.0;
        float pattern = 0.5 + 0.5 * sin(art_uv.x * 5.0 + art_uv.y * 3.0 +
                                        (float)i * 1.7 + t * 0.6);
        float ring = 1.0 - smoothstep(0.0, 0.06,
                                      abs(length(r - float2(0.0, 0.05)) - 0.42));
        float core = 1.0 - smoothstep(0.0, 0.12, length(r - float2(0.0, 0.05)));
        float3 art = base * (0.3 + 0.7 * pattern);
        art += ring * 0.22 + core * 0.12;
        art *= 0.97 + 0.03 * sin(r.y * 90.0);
        art *= 0.8 + 0.2 * (r.y * 0.5 + 0.5);
        art += 0.05 * saturate(1.0 - abs(r.x + r.y) * 1.6);
        art *= shade;

        float bar = 1.0 - smoothstep(0.0, 0.03, abs(r.y + 0.78) - 0.12);
        art = lerp(art, base * 0.16, bar * 0.8);
        float top = 1.0 - smoothstep(0.0, 0.03, abs(r.y - 0.78) - 0.12);
        art = lerp(art, base * 0.28, top * 0.5);

        float mark = 1.0 - smoothstep(0.0, 0.04, length(r - float2(-0.55, 0.62)) - 0.10);
        art += base * mark * 0.5;

        float border = 1.0 - smoothstep(0.0, 0.03, abs(sdf) - 0.01);
        art = lerp(art, float3(0.85, 0.9, 1.0), border * 0.3);

        float2 rr = float2(r.x, -2.0 - r.y);
        float rp = 0.5 + 0.5 * sin(rr.x * 5.0 - rr.y * 3.0 + (float)i * 1.7 + t * 0.6);
        float3 reflection = base * 0.55 * (0.35 + 0.65 * rp) * shade;

        if (card > 0.001 && ad < best) {
            best = ad;
            col = lerp(col, art, card);
        }
        if (rmask > 0.001 && ad < best) {
            best = ad;
            col = lerp(col, reflection, rmask);
        }
    }

    col *= saturate(t * 1.2);
    col *= 1.0 - 0.28 * length(q);

    float mask = text_mask(uv);
    col = lerp(col, float3(0.75, 0.9, 1.0), mask * 0.9);

    return float4(col, 1.0);
}
