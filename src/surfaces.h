// surfaces.h - parametric surfaces + tube (knot) mesh generation. No OpenGL here.
#pragma once
#include "math3d.h"
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

static const float PI = 3.14159265358979323846f;

enum class SurfType { TorusKnot, Seashell, Klein, Dini, Breather, Kuen, COUNT };

struct SurfaceInfo {
    const char* name;
    int   uSegs, vSegs;   // tessellation
    int   hueAxis;        // 0 = hue runs along u, 1 = along v
    float hueScale;       // number of colour cycles
    float twist;          // diagonal colour twist (screenshot-style bands)
    float gridU, gridV;   // wireframe line density
};

inline SurfaceInfo surfaceInfo(SurfType t) {
    switch (t) {
    case SurfType::TorusKnot: return { "Torus Knot",  760, 44,  0, 3.0f, 0.0f, 190, 12 };
    case SurfType::Seashell:  return { "Seashell",    420, 90,  0, 2.0f, 0.0f,  96, 30 };
    case SurfType::Klein:     return { "Klein Bottle", 200, 130, 0, 1.0f, 0.0f,  64, 40 };
    case SurfType::Dini:      return { "Dini",        460, 60,  0, 3.0f, 0.0f, 130, 22 };
    case SurfType::Breather:  return { "Breather",    320, 320, 1, 1.0f, 0.0f,  70, 70 };
    case SurfType::Kuen:      return { "Kuen",        260, 210, 0, 2.0f, 0.0f,  64, 44 };
    default:                  return { "?", 64, 64, 0, 1.0f, 0.0f, 32, 32 };
    }
}

// Geometry parameters that require a mesh rebuild when changed.
struct SurfaceParams {
    SurfType type = SurfType::TorusKnot;
    int      uSegs = 760;
    int      vSegs = 44;
    int      p = 2;      // torus knot winding (long)
    int      q = 3;      // torus knot winding (meridian)
    float    tubeRadius = 0.55f;  // torus knot tube radius
};

struct MeshData {
    std::vector<float>    verts;  // interleaved: px,py,pz, nx,ny,nz, u,v  (stride 8)
    std::vector<unsigned> idx;
    std::string           title;
};

// ---- parametric surfaces: u,v in [0,1] -----------------------------------

inline Vec3 evalSeashell(float a, float b) {
    float u = a * 6.0f * PI;               // 0 .. 6pi  (spiral)
    float v = b * 2.0f * PI;               // 0 .. 2pi  (around tube)
    float e = std::exp(u / (6.0f * PI));
    float cv = std::cos(v * 0.5f); cv *= cv;
    float x = 2.0f * (1.0f - e) * std::cos(u) * cv;
    float y = 2.0f * (-1.0f + e) * std::sin(u) * cv;
    float z = 1.0f - std::exp(u / (3.0f * PI)) - std::sin(v) + e * std::sin(v);
    return { x, y, z };
}

inline Vec3 evalKlein(float a, float b) {   // figure-8 immersion
    float u = a * 2.0f * PI, v = b * 2.0f * PI;
    float r = 2.5f;
    float cu2 = std::cos(u * 0.5f), su2 = std::sin(u * 0.5f);
    float sv = std::sin(v), s2v = std::sin(2.0f * v);
    float f = r + cu2 * sv - su2 * s2v;
    return { f * std::cos(u), f * std::sin(u), su2 * sv + cu2 * s2v };
}

inline Vec3 evalDini(float a, float b) {    // twisted pseudosphere
    float u = a * 4.0f * PI;
    float v = 0.08f + b * (2.0f - 0.08f);   // stay off the v=0 singularity
    float sv = std::sin(v);
    float z = std::cos(v) + std::log(std::tan(v * 0.5f)) + 0.22f * u;
    return { std::cos(u) * sv, std::sin(u) * sv, z };
}

inline Vec3 evalBreather(float a, float b) {
    const float bb = 0.4f;
    const float w = std::sqrt(1.0f - bb * bb);
    float u = (a * 2.0f - 1.0f) * 13.2f;
    float v = (b * 2.0f - 1.0f) * 37.4f;
    float bu = bb * u, wv = w * v;
    float ch = std::cosh(bu), sh = std::sinh(bu);
    float denom = bb * ((w * ch) * (w * ch) + (bb * std::sin(wv)) * (bb * std::sin(wv)));
    if (std::fabs(denom) < 1e-6f) denom = (denom < 0 ? -1.0f : 1.0f) * 1e-6f;
    float x = -u + (2.0f * (1.0f - bb * bb) * ch * sh) / denom;
    float y = (2.0f * w * ch * (-w * std::cos(v) * std::cos(wv) - std::sin(v) * std::sin(wv))) / denom;
    float z = (2.0f * w * ch * (-w * std::sin(v) * std::cos(wv) + std::cos(v) * std::sin(wv))) / denom;
    return { x, y, z };
}

inline Vec3 evalKuen(float a, float b) {
    float u = (a * 2.0f - 1.0f) * 4.5f;
    float v = 0.05f + b * (PI - 0.12f);
    float su = std::sin(u), cu = std::cos(u), sv = std::sin(v);
    float den = 1.0f + u * u * sv * sv;
    float x = 2.0f * (cu + u * su) * sv / den;
    float y = 2.0f * (su - u * cu) * sv / den;
    float z = std::log(std::tan(v * 0.5f)) + 2.0f * std::cos(v) / den;
    return { x, y, z };
}

inline Vec3 evalParam(SurfType t, float u, float v) {
    switch (t) {
    case SurfType::Seashell: return evalSeashell(u, v);
    case SurfType::Klein:    return evalKlein(u, v);
    case SurfType::Dini:     return evalDini(u, v);
    case SurfType::Breather: return evalBreather(u, v);
    case SurfType::Kuen:     return evalKuen(u, v);
    default:                 return { 0, 0, 0 };
    }
}

// (p,q) torus-knot centre curve, t in [0, 2pi]
inline Vec3 knotCurve(float t, int p, int q) {
    float r = std::cos(q * t) + 2.0f;
    return { r * std::cos(p * t), r * std::sin(p * t), -std::sin(q * t) };
}

// ---- helpers --------------------------------------------------------------

inline void centerAndScale(std::vector<float>& v, float target = 2.6f) {
    if (v.empty()) return;
    float mn[3] = { 1e30f,  1e30f,  1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < v.size(); i += 8)
        for (int k = 0; k < 3; k++) {
            mn[k] = std::min(mn[k], v[i + k]);
            mx[k] = std::max(mx[k], v[i + k]);
        }
    float ctr[3], ext = 0;
    for (int k = 0; k < 3; k++) { ctr[k] = 0.5f * (mn[k] + mx[k]); ext = std::max(ext, mx[k] - mn[k]); }
    float s = ext > 1e-9f ? target / ext : 1.0f;
    for (size_t i = 0; i < v.size(); i += 8)
        for (int k = 0; k < 3; k++) v[i + k] = (v[i + k] - ctr[k]) * s;
}

inline void gridIndices(std::vector<unsigned>& idx, int US, int VS, bool reverse = false) {
    for (int i = 0; i < US; i++)
        for (int j = 0; j < VS; j++) {
            unsigned a = static_cast<unsigned>(i * (VS + 1) + j);
            unsigned b = a + 1;
            unsigned c = a + (VS + 1);
            unsigned d = c + 1;
            if (!reverse) {
                idx.push_back(a); idx.push_back(c); idx.push_back(b);
                idx.push_back(b); idx.push_back(c); idx.push_back(d);
            }
            else {
                idx.push_back(a); idx.push_back(b); idx.push_back(c);
                idx.push_back(b); idx.push_back(d); idx.push_back(c);
            }
        }
}

// ---- main builder ---------------------------------------------------------

inline MeshData buildMesh(const SurfaceParams& sp) {
    MeshData md;
    const int US = std::max(2, sp.uSegs);
    const int VS = std::max(2, sp.vSegs);

    if (sp.type == SurfType::TorusKnot) {
        // Sample curve + unit tangents.
        std::vector<Vec3> C(US + 1), T(US + 1);
        for (int i = 0; i <= US; i++) {
            float t = static_cast<float>(i) / US * 2.0f * PI;
            C[i] = knotCurve(t, sp.p, sp.q);
            float dt = 1e-3f;
            T[i] = normalize(knotCurve(t + dt, sp.p, sp.q) - knotCurve(t - dt, sp.p, sp.q));
        }
        // Rotation-minimizing frame (double reflection, Wang et al. 2008).
        std::vector<Vec3> R(US + 1);
        Vec3 axis = (std::fabs(T[0].z) < 0.9f) ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
        R[0] = normalize(cross(T[0], axis));
        for (int i = 0; i < US; i++) {
            Vec3 v1 = C[i + 1] - C[i];
            float c1 = dot(v1, v1);
            c1 = std::max(c1, 1e-20f);
            Vec3 rL = R[i] - v1 * (2.0f / c1 * dot(v1, R[i]));
            Vec3 tL = T[i] - v1 * (2.0f / c1 * dot(v1, T[i]));
            Vec3 v2 = T[i + 1] - tL;
            float c2 = dot(v2, v2);
            c2 = std::max(c2, 1e-20f);
            R[i + 1] = normalize(rL - v2 * (2.0f / c2 * dot(v2, rL)));
        }
        // Distribute closure mismatch so the tube seam matches smoothly.
        float cosA = std::max(-1.0f, std::min(1.0f, dot(R[US], R[0])));
        float sinA = dot(cross(R[US], R[0]), T[0]);
        float residual = std::atan2(sinA, cosA);

        md.verts.reserve((size_t)(US + 1) * (VS + 1) * 8);
        for (int i = 0; i <= US; i++) {
            float phi = residual * ((float)i / US);
            float cp = std::cos(phi), spn = std::sin(phi);
            Vec3 s = cross(T[i], R[i]);
            Vec3 V1 = R[i] * cp + s * spn;   // rotate R[i] about T[i] by phi
            Vec3 V2 = cross(T[i], V1);
            for (int j = 0; j <= VS; j++) {
                float th = static_cast<float>(j) / VS * 2.0f * PI;
                Vec3 dir = V1 * std::cos(th) + V2 * std::sin(th);
                Vec3 pnt = C[i] + dir * sp.tubeRadius;
                float uv0 = static_cast<float>(i) / US, uv1 = static_cast<float>(j) / VS;
                float d[8] = { pnt.x, pnt.y, pnt.z, dir.x, dir.y, dir.z, uv0, uv1 };
                md.verts.insert(md.verts.end(), d, d + 8);
            }
        }
        gridIndices(md.idx, US, VS, /*reverse=*/true);   // outward normals -> front faces
    }
    else {
        const float eps = 1e-3f;
        md.verts.reserve((size_t)(US + 1) * (VS + 1) * 8);
        for (int i = 0; i <= US; i++) {
            float u = (float)i / US;
            for (int j = 0; j <= VS; j++) {
                float v = (float)j / VS;
                Vec3 p = evalParam(sp.type, u, v);
                Vec3 pu = evalParam(sp.type, std::min(u + eps, 1.0f), v)
                    - evalParam(sp.type, std::max(u - eps, 0.0f), v);
                Vec3 pv = evalParam(sp.type, u, std::min(v + eps, 1.0f))
                    - evalParam(sp.type, u, std::max(v - eps, 0.0f));
                Vec3 n = cross(pu, pv);
                if (length(n) < 1e-8f) n = normalize(p);
                else                   n = normalize(n);
                float d[8] = { p.x, p.y, p.z, n.x, n.y, n.z, u, v };
                md.verts.insert(md.verts.end(), d, d + 8);
            }
        }
        gridIndices(md.idx, US, VS);
    }

    centerAndScale(md.verts);

    SurfaceInfo info = surfaceInfo(sp.type);
    md.title = info.name;

    if (sp.type == SurfType::TorusKnot)
    {
        md.title += " | p=" + std::to_string(sp.p) + " q=" + std::to_string(sp.q);
    }

    return md;
}
