// surfaces.h - the parametric surfaces and their triangle meshes. No OpenGL here.
#pragma once

#include "math3d.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

inline constexpr float kPi = std::numbers::pi_v<float>;

enum class Surface { TorusKnot, Seashell, Klein, Dini, Breather, Kuen };
inline constexpr int kSurfaceCount = static_cast<int>(Surface::Kuen) + 1;

// Tessellation and default colouring of a surface.
struct SurfaceInfo {
    std::string_view name;
    int   uSegments, vSegments;
    int   hueAxis;          // 0 = hue runs along u, 1 = along v
    float hueScale;         // number of colour cycles
    float twist;            // diagonal colour twist
    float gridU, gridV;     // wireframe line density
};

[[nodiscard]] constexpr SurfaceInfo surfaceInfo(Surface surface) {
    switch (surface) {
    case Surface::TorusKnot: return { "Torus Knot",   760,  44, 0, 3.0f, 0.0f, 190, 12 };
    case Surface::Seashell:  return { "Seashell",     420,  90, 0, 2.0f, 0.0f,  96, 30 };
    case Surface::Klein:     return { "Klein Bottle", 200, 130, 0, 1.0f, 0.0f,  64, 40 };
    case Surface::Dini:      return { "Dini",         460,  60, 0, 3.0f, 0.0f, 130, 22 };
    case Surface::Breather:  return { "Breather",     320, 320, 1, 1.0f, 0.0f,  70, 70 };
    case Surface::Kuen:      return { "Kuen",         260, 210, 0, 2.0f, 0.0f,  64, 44 };
    }
    std::unreachable();
}

// The torus knot: a tube of the given radius around a (p, q) torus knot, or around
// all gcd(p, q) components of the torus link when p and q are not coprime.
struct KnotParams {
    int   p = 2;                // turns around the axis of the torus
    int   q = 3;                // turns around its core circle
    float tubeRadius = 0.55f;
};

inline constexpr int kMaxKnotTurns = 16;

struct Vertex {
    Vec3  position;
    Vec3  normal;
    float u = 0, v = 0;         // surface parameters, 0..1
};

struct Mesh {
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;   // triangle list
};

// The mesh of a surface, centered and scaled to the same view box for every surface.
[[nodiscard]] Mesh buildMesh(Surface surface, const KnotParams& knot);

// ---- parametric surfaces: u, v in [0, 1] ------------------------------------

[[nodiscard]] constexpr Vec3 evalSeashell(float a, float b) {
    const float u = a * 6.0f * kPi;               // 0 .. 6 pi (spiral)
    const float v = b * 2.0f * kPi;               // 0 .. 2 pi (around the tube)
    const float e = std::exp(u / (6.0f * kPi));
    const float c = std::cos(v * 0.5f);
    const float cv = c * c;
    return { 2.0f * (1.0f - e) * std::cos(u) * cv,
             2.0f * (-1.0f + e) * std::sin(u) * cv,
             1.0f - std::exp(u / (3.0f * kPi)) - std::sin(v) + e * std::sin(v) };
}

[[nodiscard]] constexpr Vec3 evalKlein(float a, float b) {      // figure-8 immersion
    const float u = a * 2.0f * kPi, v = b * 2.0f * kPi;
    const float r = 2.5f;
    const float cu2 = std::cos(u * 0.5f), su2 = std::sin(u * 0.5f);
    const float sv = std::sin(v), s2v = std::sin(2.0f * v);
    const float f = r + cu2 * sv - su2 * s2v;
    return { f * std::cos(u), f * std::sin(u), su2 * sv + cu2 * s2v };
}

[[nodiscard]] constexpr Vec3 evalDini(float a, float b) {       // twisted pseudosphere
    const float u = a * 4.0f * kPi;
    const float v = 0.08f + b * (2.0f - 0.08f);   // stays off the singularity at v = 0
    const float sv = std::sin(v);
    return { std::cos(u) * sv,
             std::sin(u) * sv,
             std::cos(v) + std::log(std::tan(v * 0.5f)) + 0.22f * u };
}

[[nodiscard]] constexpr Vec3 evalBreather(float a, float b) {
    const float bb = 0.4f;
    const float w = std::sqrt(1.0f - bb * bb);
    const float u = (a * 2.0f - 1.0f) * 13.2f;
    const float v = (b * 2.0f - 1.0f) * 37.4f;
    const float ch = std::cosh(bb * u), sh = std::sinh(bb * u);
    const float swv = std::sin(w * v), cwv = std::cos(w * v);
    const float denom = bb * ((w * ch) * (w * ch) + (bb * swv) * (bb * swv));   // >= bb * w^2 > 0
    return { -u + (2.0f * (1.0f - bb * bb) * ch * sh) / denom,
             (2.0f * w * ch * (-w * std::cos(v) * cwv - std::sin(v) * swv)) / denom,
             (2.0f * w * ch * (-w * std::sin(v) * cwv + std::cos(v) * swv)) / denom };
}

[[nodiscard]] constexpr Vec3 evalKuen(float a, float b) {
    const float u = (a * 2.0f - 1.0f) * 4.5f;
    const float v = 0.05f + b * (kPi - 0.12f);
    const float su = std::sin(u), cu = std::cos(u), sv = std::sin(v);
    const float den = 1.0f + u * u * sv * sv;
    return { 2.0f * (cu + u * su) * sv / den,
             2.0f * (su - u * cu) * sv / den,
             std::log(std::tan(v * 0.5f)) + 2.0f * std::cos(v) / den };
}

// ---- torus knot ---------------------------------------------------------------

// The (p, q) torus knot centre curve and its derivative dC/dt, t in [0, 2 pi].
[[nodiscard]] constexpr Vec3 knotCurve(float t, float p, float q) {
    const float r = std::cos(q * t) + 2.0f;
    return { r * std::cos(p * t), r * std::sin(p * t), -std::sin(q * t) };
}

[[nodiscard]] constexpr Vec3 knotTangent(float t, float p, float q) {
    const float r  = std::cos(q * t) + 2.0f;
    const float dr = -q * std::sin(q * t);
    const float cp = std::cos(p * t), sp = std::sin(p * t);
    return { dr * cp - r * p * sp, dr * sp + r * p * cp, -q * std::cos(q * t) };
}
