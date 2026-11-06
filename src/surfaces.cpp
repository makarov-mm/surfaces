#include "surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>

namespace {

// Two triangles per cell of a (uSegments + 1) x (vSegments + 1) vertex grid that
// starts at vertex `first`; `flip` reverses the winding.
void appendGridIndices(std::vector<std::uint32_t>& indices, std::uint32_t first,
                       int uSegments, int vSegments, bool flip) {
    const auto stride = static_cast<std::uint32_t>(vSegments + 1);
    indices.reserve(indices.size() + static_cast<std::size_t>(uSegments) * static_cast<std::size_t>(vSegments) * 6);
    for (int i = 0; i < uSegments; ++i)
        for (int j = 0; j < vSegments; ++j) {
            const std::uint32_t a = first + static_cast<std::uint32_t>(i) * stride + static_cast<std::uint32_t>(j);
            const std::uint32_t b = a + 1, c = a + stride, d = c + 1;
            if (flip)
                indices.insert(indices.end(), { a, b, c, b, d, c });
            else
                indices.insert(indices.end(), { a, c, b, b, c, d });
        }
}

// Moves the center of the bounding box to the origin and scales the largest extent to `size`.
void centerAndScale(std::span<Vertex> vertices, float size = 2.6f) {
    if (vertices.empty())
        return;
    Vec3 lo = vertices.front().position, hi = lo;
    for (const Vertex& vertex : vertices) {
        const Vec3& p = vertex.position;
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    const Vec3  center = (lo + hi) * 0.5f;
    const float extent = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z });
    const float scale  = extent > 1e-9f ? size / extent : 1.0f;
    for (Vertex& vertex : vertices)
        vertex.position = (vertex.position - center) * scale;
}

// A surface given by a map of [0, 1]^2, with finite-difference normals.
template <Vec3 (*eval)(float, float)>
Mesh buildParametric(const SurfaceInfo& info) {
    const int US = info.uSegments, VS = info.vSegments;
    constexpr float h = 1e-3f;   // finite-difference step

    Mesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(US + 1) * static_cast<std::size_t>(VS + 1));
    for (int i = 0; i <= US; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(US);
        for (int j = 0; j <= VS; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(VS);
            const Vec3 p = eval(u, v);
            // One-sided differences at the edges of the parameter domain.
            const Vec3 du = eval(std::min(u + h, 1.0f), v) - eval(std::max(u - h, 0.0f), v);
            const Vec3 dv = eval(u, std::min(v + h, 1.0f)) - eval(u, std::max(v - h, 0.0f));
            const Vec3 n  = cross(du, dv);
            // Where the surface degenerates to a point, the radial direction stands in.
            mesh.vertices.push_back({ p, length(n) < 1e-8f ? normalize(p) : normalize(n), u, v });
        }
    }
    appendGridIndices(mesh.indices, 0, US, VS, false);
    return mesh;
}

// A tube around the torus knot, with exact (radial) normals.
Mesh buildKnot(const SurfaceInfo& info, const KnotParams& knot) {
    const float p = static_cast<float>(knot.p);
    const float q = static_cast<float>(knot.q);

    // For gcd(p, q) = n > 1 the curve closes after 1/n of the parameter range. That
    // is one component of the torus link; the others are copies of it rotated about
    // the z axis by 2 pi k / q.
    const int components = std::gcd(knot.p, knot.q);

    // Segments along the curve in proportion to its length: |C'(t)|^2 = q^2 + p^2 r^2
    // with r = 2 + cos(qt), whose mean square is 4.5. The (2, 3) knot keeps info.uSegments.
    const float lengthRatio = std::sqrt((q * q + 4.5f * p * p) / (3.0f * 3.0f + 4.5f * 2.0f * 2.0f))
                            / static_cast<float>(components);
    const int US = std::max(32, static_cast<int>(std::lround(static_cast<float>(info.uSegments) * lengthRatio)));
    const int VS = info.vSegments;

    // Centre curve and unit tangents.
    std::vector<Vec3> C(US + 1), T(US + 1);
    for (int i = 0; i <= US; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(US) * 2.0f * kPi / static_cast<float>(components);
        C[i] = knotCurve(t, p, q);
        T[i] = normalize(knotTangent(t, p, q));
    }

    // Rotation-minimizing frame (double reflection, Wang et al. 2008).
    std::vector<Vec3> R(US + 1);
    R[0] = normalize(cross(T[0], std::fabs(T[0].z) < 0.9f ? Vec3{ 0, 0, 1 } : Vec3{ 0, 1, 0 }));
    for (int i = 0; i < US; ++i) {
        const Vec3  v1 = C[i + 1] - C[i];
        const float c1 = std::max(dot(v1, v1), 1e-20f);
        const Vec3  rL = R[i] - v1 * (2.0f / c1 * dot(v1, R[i]));
        const Vec3  tL = T[i] - v1 * (2.0f / c1 * dot(v1, T[i]));
        const Vec3  v2 = T[i + 1] - tL;
        const float c2 = std::max(dot(v2, v2), 1e-20f);
        R[i + 1] = normalize(rL - v2 * (2.0f / c2 * dot(v2, rL)));
    }

    // After one round the frame comes back turned about the tangent. Spreading that
    // angle along the curve closes the tube without a seam.
    const float closure = std::atan2(dot(cross(R[US], R[0]), T[0]),
                                     std::clamp(dot(R[US], R[0]), -1.0f, 1.0f));

    const std::size_t tubeVertices = static_cast<std::size_t>(US + 1) * static_cast<std::size_t>(VS + 1);
    Mesh mesh;
    mesh.vertices.reserve(tubeVertices * static_cast<std::size_t>(components));
    for (int i = 0; i <= US; ++i) {
        const float u   = static_cast<float>(i) / static_cast<float>(US);
        const float phi = closure * u;
        const Vec3  n1  = R[i] * std::cos(phi) + cross(T[i], R[i]) * std::sin(phi);   // R[i] turned by phi about T[i]
        const Vec3  n2  = cross(T[i], n1);
        for (int j = 0; j <= VS; ++j) {
            const float v      = static_cast<float>(j) / static_cast<float>(VS);
            const float theta  = v * 2.0f * kPi;
            const Vec3  radial = n1 * std::cos(theta) + n2 * std::sin(theta);
            mesh.vertices.push_back({ C[i] + radial * knot.tubeRadius, radial, u, v });
        }
    }

    // The other components of a torus link.
    for (int k = 1; k < components; ++k) {
        const float angle = 2.0f * kPi * static_cast<float>(k) / q;
        const float c = std::cos(angle), s = std::sin(angle);
        const auto rotate = [c, s](const Vec3& a) { return Vec3{ c * a.x - s * a.y, s * a.x + c * a.y, a.z }; };
        for (std::size_t n = 0; n < tubeVertices; ++n) {
            const Vertex vertex = mesh.vertices[n];
            mesh.vertices.push_back({ rotate(vertex.position), rotate(vertex.normal), vertex.u, vertex.v });
        }
    }

    for (int k = 0; k < components; ++k)   // flipped winding: the outward normals face front
        appendGridIndices(mesh.indices, static_cast<std::uint32_t>(static_cast<std::size_t>(k) * tubeVertices), US, VS, true);
    return mesh;
}

} // namespace

Mesh buildMesh(Surface surface, const KnotParams& knot) {
    const SurfaceInfo info = surfaceInfo(surface);
    Mesh mesh;
    switch (surface) {
    case Surface::TorusKnot: mesh = buildKnot(info, knot);                   break;
    case Surface::Seashell:  mesh = buildParametric<evalSeashell>(info);     break;
    case Surface::Klein:     mesh = buildParametric<evalKlein>(info);        break;
    case Surface::Dini:      mesh = buildParametric<evalDini>(info);         break;
    case Surface::Breather:  mesh = buildParametric<evalBreather>(info);     break;
    case Surface::Kuen:      mesh = buildParametric<evalKuen>(info);         break;
    }
    centerAndScale(mesh.vertices);
    return mesh;
}
