// math3d.h - tiny self-contained linear algebra (column-major, OpenGL style).
// Everything is constexpr and [[nodiscard]]; the algebraic primitives are usable
// in constant expressions (see the static_asserts below). Functions that call
// <cmath> stay constexpr thanks to C++23 P2448 (relaxed constexpr), so they work
// at run time now and become constant-evaluable once the toolchain ships
// constexpr <cmath> (C++26 P1383) with no source change.
#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    [[nodiscard]] constexpr Vec3 operator*(float s)       const { return {x * s, y * s, z * s}; }
};

[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3  cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
[[nodiscard]] constexpr Vec3  normalize(const Vec3& a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3(0, 0, 1);
}

// Compile-time sanity checks (algebraic ops only, no transcendental).
static_assert(dot(Vec3(1, 0, 0), Vec3(1, 0, 0)) == 1.0f);
static_assert(dot(cross(Vec3(1, 0, 0), Vec3(0, 1, 0)), Vec3(0, 0, 1)) == 1.0f);

struct Mat4 {                    // column-major: m[col*4 + row]
    float m[16];
    [[nodiscard]] static constexpr Mat4 identity() {
        Mat4 r{};
        for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

[[nodiscard]] constexpr Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

[[nodiscard]] constexpr Mat4 translate(float x, float y, float z) {
    Mat4 r = Mat4::identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}
[[nodiscard]] constexpr Mat4 scaleUniform(float s) {
    Mat4 r = Mat4::identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}
[[nodiscard]] constexpr Mat4 rotateX(float a) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
[[nodiscard]] constexpr Mat4 rotateY(float a) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

[[nodiscard]] constexpr Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r{};
    for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
    const float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

[[nodiscard]] constexpr Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// upper-left 3x3 as 9 floats, column-major (used for the normal matrix)
constexpr void mat3from(const Mat4& a, float out[9]) {
    out[0] = a.m[0]; out[1] = a.m[1]; out[2] = a.m[2];
    out[3] = a.m[4]; out[4] = a.m[5]; out[5] = a.m[6];
    out[6] = a.m[8]; out[7] = a.m[9]; out[8] = a.m[10];
}
