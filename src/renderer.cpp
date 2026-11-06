#include "renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModelView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vViewPos;
out vec3 vViewNormal;
out vec2 vUV;

void main() {
    vec4 viewPos = uModelView * vec4(aPosition, 1.0);
    vViewPos    = viewPos.xyz;
    vViewNormal = uNormalMatrix * aNormal;
    vUV         = aUV;
    gl_Position = uProjection * viewPos;
}
)";

// Blinn-Phong with a headlight, hue from one surface parameter (sheared by the
// twist along the other one), and a wireframe drawn from the UV coordinates.
constexpr const char* kFragmentShader = R"(#version 330 core
in vec3 vViewPos;
in vec3 vViewNormal;
in vec2 vUV;

out vec4 fragColor;

uniform int   uHueAxis;     // 0 = hue runs along u, 1 = along v
uniform float uHueScale;    // colour cycles
uniform float uTwist;       // diagonal colour twist
uniform vec2  uGrid;        // wireframe lines along u and v
uniform bool  uWireframe;

const float kSaturation    = 0.95;
const float kValue         = 1.0;
const float kAmbient       = 0.18;
const float kSpecular      = 0.7;
const float kShininess     = 48.0;
const float kWireStrength  = 0.85;
const vec3  kWireColor     = vec3(0.015);
const vec3  kLightDir      = normalize(vec3(0.35, 0.55, 0.80));   // view space

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec3 N = normalize(gl_FrontFacing ? vViewNormal : -vViewNormal);   // lit from both sides
    vec3 V = normalize(-vViewPos);
    vec3 H = normalize(kLightDir + V);
    float diffuse  = max(dot(N, kLightDir), 0.0);
    float specular = pow(max(dot(N, H), 0.0), kShininess);

    vec2 uv = (uHueAxis == 0) ? vUV : vUV.yx;   // x: the hue axis, y: the other one
    float hue = fract(uv.x * uHueScale + uTwist * uv.y);
    vec3 albedo = hsv2rgb(vec3(hue, kSaturation, kValue));
    vec3 color = albedo * (kAmbient + diffuse) + vec3(specular * kSpecular);

    if (uWireframe) {
        // Distance to the nearest grid line in pixels, so the lines stay one pixel wide.
        vec2 g = vUV * uGrid;
        vec2 toLine = abs(fract(g - 0.5) - 0.5) / fwidth(g);
        float line = 1.0 - clamp(min(toLine.x, toLine.y), 0.0, 1.0);
        color = mix(color, kWireColor, line * kWireStrength);
    }
    fragColor = vec4(color, 1.0);
}
)";

constexpr float kFieldOfView = 45.0f * kPi / 180.0f;   // vertical
constexpr float kNearPlane   = 0.1f;
constexpr float kFarPlane    = 100.0f;

// Shaders and programs report errors the same way, through their own pair of functions.
std::string infoLog(GLuint object, PFNGLGETSHADERIVPROC getParameter, PFNGLGETSHADERINFOLOGPROC getLog) {
    GLint length = 0;
    getParameter(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GLsizei written = 0;
    getLog(object, static_cast<GLsizei>(log.size()), &written, log.data());
    log.resize(static_cast<std::size_t>(written));
    return log;
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        const std::string log = infoLog(shader, glGetShaderiv, glGetShaderInfoLog);
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed:\n" + log);
    }
    return shader;
}

GLuint buildProgram() {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragmentShader = 0;
    try {
        fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    } catch (...) {
        glDeleteShader(vertexShader);
        throw;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);     // freed together with the program
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        const std::string log = infoLog(program, glGetProgramiv, glGetProgramInfoLog);
        glDeleteProgram(program);
        throw std::runtime_error("Shader linking failed:\n" + log);
    }
    return program;
}

void setFloatAttribute(GLuint index, GLint components, std::size_t offset) {
    glVertexAttribPointer(index, components, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Vertex)),
                          reinterpret_cast<const void*>(offset));
    glEnableVertexAttribArray(index);
}

} // namespace

SurfaceRenderer::SurfaceRenderer()
    : m_program(buildProgram()) {
    const auto uniform = [this](const char* name) { return glGetUniformLocation(m_program, name); };
    m_uniforms = {
        .modelView    = uniform("uModelView"),
        .projection   = uniform("uProjection"),
        .normalMatrix = uniform("uNormalMatrix"),
        .hueAxis      = uniform("uHueAxis"),
        .hueScale     = uniform("uHueScale"),
        .twist        = uniform("uTwist"),
        .grid         = uniform("uGrid"),
        .wireframe    = uniform("uWireframe"),
    };

    glGenVertexArrays(1, &m_vertexArray);
    glGenBuffers(1, &m_vertexBuffer);
    glGenBuffers(1, &m_indexBuffer);
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);   // part of the vertex array state
    setFloatAttribute(0, 3, offsetof(Vertex, position));
    setFloatAttribute(1, 3, offsetof(Vertex, normal));
    setFloatAttribute(2, 2, offsetof(Vertex, u));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

SurfaceRenderer::~SurfaceRenderer() {
    glDeleteBuffers(1, &m_indexBuffer);
    glDeleteBuffers(1, &m_vertexBuffer);
    glDeleteVertexArrays(1, &m_vertexArray);
    glDeleteProgram(m_program);
}

void SurfaceRenderer::upload(const Mesh& mesh) {
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    m_indexCount = static_cast<GLsizei>(mesh.indices.size());
}

void SurfaceRenderer::render(int width, int height, const Camera& camera, const Style& style) const {
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Mat4 model      = mul(rotateY(camera.yaw), rotateX(camera.pitch));
    const Mat4 view       = lookAt({ 0, 0, camera.distance }, { 0, 0, 0 }, { 0, 1, 0 });
    const Mat4 modelView  = mul(view, model);
    const Mat4 projection = perspective(kFieldOfView, static_cast<float>(width) / static_cast<float>(height),
                                        kNearPlane, kFarPlane);
    // modelView only rotates and translates, so its rotation part turns the normals too.
    const std::array<float, 9> normalMatrix = upperLeft3x3(modelView);

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uniforms.modelView, 1, GL_FALSE, modelView.m.data());
    glUniformMatrix4fv(m_uniforms.projection, 1, GL_FALSE, projection.m.data());
    glUniformMatrix3fv(m_uniforms.normalMatrix, 1, GL_FALSE, normalMatrix.data());
    glUniform1i(m_uniforms.hueAxis, style.hueAxis);
    glUniform1f(m_uniforms.hueScale, style.hueScale);
    glUniform1f(m_uniforms.twist, style.twist);
    glUniform2f(m_uniforms.grid, style.gridU, style.gridV);
    glUniform1i(m_uniforms.wireframe, style.wireframe ? GL_TRUE : GL_FALSE);

    glBindVertexArray(m_vertexArray);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
