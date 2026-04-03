#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#define M_PI 3.1415926535897932384626433832795f
#define M_PI_INV (1.0 / 3.1415926535897932384626433832795)
#define M_FLOAT_EPS 0.000001f

#define BLACK 0.0f
#define WHITE 1.0f

float3x3 transpose3x3(float3x3 m) {
    return float3x3(
        float3(m[0][0], m[1][0], m[2][0]),
        float3(m[0][1], m[1][1], m[2][1]),
        float3(m[0][2], m[1][2], m[2][2])
    );
}

/// 3×3 inverse via cofactors (no `import glsl` / no HLSL `inverse` for float3x3 here).
float3x3 inverse3x3(float3x3 m) {
    float a = m[0][0], b = m[0][1], c = m[0][2];
    float d = m[1][0], e = m[1][1], f = m[1][2];
    float g = m[2][0], h = m[2][1], i = m[2][2];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    float invDet = 1.0 / det;
    float3 row0 = float3(e * i - f * h, c * h - b * i, b * f - c * e) * invDet;
    float3 row1 = float3(f * g - d * i, a * i - c * g, c * d - a * f) * invDet;
    float3 row2 = float3(d * h - e * g, b * g - a * h, a * e - b * d) * invDet;
    return float3x3(row0, row1, row2);
}

#endif // __GLOBALS_H__