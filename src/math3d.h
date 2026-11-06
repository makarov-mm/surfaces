// math3d.h - tiny self-contained linear algebra (column-major, OpenGL style).
// Everything is constexpr and [[nodiscard]]. The functions that call <cmath> are
// constexpr too (relaxed constexpr, C++23 P2448): they run at run time today and
// become constant-evaluable once the library ships constexpr <cmath> (C++26 P1383),
// with no source change.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

struct Vec3 {
    float x = 0, y = 0, z = 0;

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    [[nodiscard]] constexpr Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
};

[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

[[nodiscard]] constexpr float length(const Vec3& a) {
    return std::sqrt(dot(a, a));
}

[[nodiscard]] constexpr Vec3 normalize(const Vec3& a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3{ 0, 0, 1 };
}

struct Mat4 {
    std::array<float, 16> m{};   // column-major: m[column * 4 + row]

    [[nodiscard]] static constexpr Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

[[nodiscard]] constexpr Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (std::size_t k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * b.m[column * 4 + k];
            r.m[column * 4 + row] = s;
        }
    return r;
}

[[nodiscard]] constexpr Mat4 rotateX(float angle) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(angle), s = std::sin(angle);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

[[nodiscard]] constexpr Mat4 rotateY(float angle) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(angle), s = std::sin(angle);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

[[nodiscard]] constexpr Mat4 perspective(float fovy, float aspect, float zNear, float zFar) {
    Mat4 r;
    const float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

[[nodiscard]] constexpr Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// The upper-left 3x3 block, column-major (the normal matrix of a rigid transform).
[[nodiscard]] constexpr std::array<float, 9> upperLeft3x3(const Mat4& a) {
    return { a.m[0], a.m[1], a.m[2],
             a.m[4], a.m[5], a.m[6],
             a.m[8], a.m[9], a.m[10] };
}
