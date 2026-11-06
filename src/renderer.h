// renderer.h - draws a surface mesh with OpenGL 3.3 core.
#pragma once

#include "surfaces.h"

#include <glad/gl.h>

// Orbits the origin; the surface turns in front of it.
struct Camera {
    float yaw      = 0.6f;    // radians
    float pitch    = 0.35f;   // radians
    float distance = 4.3f;
};

// How the surface is coloured.
struct Style {
    int   hueAxis   = 0;      // 0 = hue runs along u, 1 = along v
    float hueScale  = 1.0f;   // number of colour cycles
    float twist     = 0.0f;   // diagonal colour twist
    float gridU     = 32.0f;  // wireframe lines along u
    float gridV     = 32.0f;  // and along v
    bool  wireframe = true;
};

// Needs the OpenGL 3.3 context to be current for its whole lifetime.
class SurfaceRenderer {
public:
    SurfaceRenderer();   // throws std::runtime_error with the log when the shaders don't build
    ~SurfaceRenderer();
    SurfaceRenderer(const SurfaceRenderer&) = delete;
    SurfaceRenderer& operator=(const SurfaceRenderer&) = delete;

    void upload(const Mesh& mesh);
    void render(int width, int height, const Camera& camera, const Style& style) const;

private:
    GLuint  m_program      = 0;
    GLuint  m_vertexArray  = 0;
    GLuint  m_vertexBuffer = 0;
    GLuint  m_indexBuffer  = 0;
    GLsizei m_indexCount   = 0;

    struct Uniforms {
        GLint modelView, projection, normalMatrix;
        GLint hueAxis, hueScale, twist, grid, wireframe;
    } m_uniforms{};
};
