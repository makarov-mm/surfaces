// main_win32.cpp - spectacular parametric surfaces, Win32 + WGL, OpenGL 3.3 core.
// Zero external dependencies: only opengl32 / gdi32 / user32 (all system libs).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <glad/gl.h>

#include "math3d.h"
#include "surfaces.h"

#include <format>
#include <string>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// WGL glue
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef BOOL (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);

#define WGL_DRAW_TO_WINDOW_ARB                 0x2001
#define WGL_SUPPORT_OPENGL_ARB                 0x2010
#define WGL_DOUBLE_BUFFER_ARB                  0x2011
#define WGL_PIXEL_TYPE_ARB                     0x2013
#define WGL_TYPE_RGBA_ARB                      0x202B
#define WGL_COLOR_BITS_ARB                     0x2014
#define WGL_DEPTH_BITS_ARB                     0x2022
#define WGL_STENCIL_BITS_ARB                   0x2023
#define WGL_SAMPLE_BUFFERS_ARB                 0x2041
#define WGL_SAMPLES_ARB                        0x2042
#define WGL_CONTEXT_MAJOR_VERSION_ARB          0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB          0x2092
#define WGL_CONTEXT_FLAGS_ARB                  0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB           0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB       0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002

static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
static PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB    = nullptr;
static PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT         = nullptr;

static HMODULE g_opengl32 = nullptr;

static GLADapiproc glLoader(const char* name)
{
    PROC p = wglGetProcAddress(name);

    if (p == nullptr || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1)
        p = g_opengl32 ? GetProcAddress(g_opengl32, name) : nullptr;

    return (GLADapiproc)p;
}

static const char* VERT = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out vec3 vViewPos;
out vec3 vViewNormal;
out vec2 vUV;
void main(){
    vec4 vp = uView * uModel * vec4(aPos, 1.0);
    vViewPos    = vp.xyz;
    vViewNormal = uNormalMat * aNormal;
    vUV         = aUV;
    gl_Position = uProj * vp;
}
)";

static const char* FRAG = R"(#version 330 core
in vec3 vViewPos;
in vec3 vViewNormal;
in vec2 vUV;
out vec4 FragColor;
uniform int   uHueAxis;
uniform float uHueScale;
uniform float uTwist;
uniform float uGridU, uGridV;
uniform int   uWire;
uniform float uWireStrength;
uniform float uSat, uVal;
uniform float uAmbient, uSpecStrength, uShininess;
uniform vec3  uLightDir;
vec3 hsv2rgb(vec3 c){
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
void main(){
    vec3 N = normalize(vViewNormal);
    if(!gl_FrontFacing) N = -N;
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(-vViewPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), uShininess);
    float hueBase = (uHueAxis == 0) ? vUV.x : vUV.y;
    float other   = (uHueAxis == 0) ? vUV.y : vUV.x;
    float hue = fract(hueBase * uHueScale + uTwist * other);
    vec3 albedo = hsv2rgb(vec3(hue, uSat, uVal));
    vec3 color = albedo * (uAmbient + diff) + vec3(1.0) * spec * uSpecStrength;
    if(uWire == 1){
        vec2 g  = vec2(vUV.x * uGridU, vUV.y * uGridV);
        vec2 gr = abs(fract(g - 0.5) - 0.5) / fwidth(g);
        float line = min(gr.x, gr.y);
        float w = 1.0 - clamp(line, 0.0, 1.0);
        color = mix(color, vec3(0.015), w * uWireStrength);
    }
    FragColor = vec4(color, 1.0);
}
)";

// ---------------------------------------------------------------- app state
struct App {
    int   hueAxis = 0;
    float hueScale = 3.0f;
    float twist = 0.0f;
    float gridU = 190.0f, gridV = 12.0f;
    bool  wire = true;

    SurfaceParams sp;
    bool dirty = false;
    int  indexCount = 0;

    float yaw = 0.6f, pitch = 0.35f;
    float dist = 4.3f;
    bool  autoRotate = true;
    bool  dragging = false;
    int   lastX = 0, lastY = 0;

    bool  fullscreen = false;
    RECT  savedRect{};
    DWORD savedStyle = 0;
    bool  running = true;
};

static void loadSurfaceDefaults(App& a, SurfType t) {
    SurfaceInfo in = surfaceInfo(t);
    a.sp.type = t; a.sp.uSegs = in.uSegs; a.sp.vSegs = in.vSegs;
    a.hueAxis = in.hueAxis; a.hueScale = in.hueScale; a.twist = in.twist;
    a.gridU = in.gridU; a.gridV = in.gridV;
    a.dirty = true;
}

// ---------------------------------------------------------------- gl helpers
static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); OutputDebugStringA(log); }
    return s;
}
static GLuint linkProgram() {
    GLuint vs = compile(GL_VERTEX_SHADER, VERT), fs = compile(GL_FRAGMENT_SHADER, FRAG);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); OutputDebugStringA(log); }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

static GLuint gVAO = 0, gVBO = 0, gEBO = 0;

static void uploadMesh(App& a) {
    MeshData m = buildMesh(a.sp);
    a.indexCount = (int)m.idx.size();
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, m.verts.size() * sizeof(float), m.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.idx.size() * sizeof(unsigned), m.idx.data(), GL_STATIC_DRAW);
}

static void setTitle(HWND hwnd, App& a) {
    const SurfaceInfo in = surfaceInfo(a.sp.type);
    const std::string title = std::format(
        "{}  |  hue={:.1f}  twist={:.2f}  |  1-6: surface   G: grid   Space: spin   F11: fullscreen",
        in.name, a.hueScale, a.twist);
    SetWindowTextA(hwnd, title.c_str());
}

static void toggleFullscreen(HWND hwnd, App& a) {
    if (!a.fullscreen) {
        a.savedStyle = GetWindowLongA(hwnd, GWL_STYLE);
        GetWindowRect(hwnd, &a.savedRect);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongA(hwnd, GWL_STYLE, a.savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        a.fullscreen = true;
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, a.savedStyle);
        SetWindowPos(hwnd, HWND_TOP, a.savedRect.left, a.savedRect.top,
            a.savedRect.right - a.savedRect.left, a.savedRect.bottom - a.savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        a.fullscreen = false;
    }
}

static void handleKey(HWND hwnd, App& a, WPARAM key) {
    switch (key) {
        case VK_ESCAPE: a.running = false; break;
        case VK_SPACE:  a.autoRotate = !a.autoRotate; break;
        case 'G':       a.wire = !a.wire; break;
        case VK_F11:    toggleFullscreen(hwnd, a); break;

        case '1': loadSurfaceDefaults(a, SurfType::TorusKnot); break;
        case '2': loadSurfaceDefaults(a, SurfType::Seashell);  break;
        case '3': loadSurfaceDefaults(a, SurfType::Klein);     break;
        case '4': loadSurfaceDefaults(a, SurfType::Dini);      break;
        case '5': loadSurfaceDefaults(a, SurfType::Breather);  break;
        case '6': loadSurfaceDefaults(a, SurfType::Kuen);      break;

        case VK_OEM_4:     a.twist -= 0.05f; break;                        // [
        case VK_OEM_6:     a.twist += 0.05f; break;                        // ]
        case VK_OEM_MINUS: a.hueScale = (a.hueScale > 0.4f) ? a.hueScale - 0.2f : 0.2f; break;
        case VK_OEM_PLUS:  a.hueScale += 0.2f; break;

        case 'R':
            a.yaw = 0.6f; a.pitch = 0.35f; a.dist = 4.3f;
            loadSurfaceDefaults(a, a.sp.type);
            break;

        case VK_LEFT:  if (a.sp.type == SurfType::TorusKnot) { if (a.sp.p > 1) a.sp.p--; a.dirty = true; } break;
        case VK_RIGHT: if (a.sp.type == SurfType::TorusKnot) { a.sp.p++; a.dirty = true; } break;
        case VK_DOWN:  if (a.sp.type == SurfType::TorusKnot) { if (a.sp.q > 1) a.sp.q--; a.dirty = true; } break;
        case VK_UP:    if (a.sp.type == SurfType::TorusKnot) { a.sp.q++; a.dirty = true; } break;
        case VK_OEM_1: if (a.sp.type == SurfType::TorusKnot) { a.sp.tubeRadius = (a.sp.tubeRadius > 0.1f) ? a.sp.tubeRadius - 0.05f : 0.05f; a.dirty = true; } break; // ;
        case VK_OEM_7: if (a.sp.type == SurfType::TorusKnot) { a.sp.tubeRadius += 0.05f; a.dirty = true; } break; // '
        default: return;
    }
    setTitle(hwnd, a);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CLOSE:   if (a) a->running = false; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_KEYDOWN: if (a) handleKey(hwnd, *a, wp); return 0;
        case WM_LBUTTONDOWN:
            if (a) { a->dragging = true; a->lastX = GET_X_LPARAM(lp); a->lastY = GET_Y_LPARAM(lp); SetCapture(hwnd); }
            return 0;
        case WM_LBUTTONUP:
            if (a) { a->dragging = false; ReleaseCapture(); }
            return 0;
        case WM_MOUSEMOVE:
            if (a && a->dragging) {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                a->yaw   += (x - a->lastX) * 0.006f;
                a->pitch += (y - a->lastY) * 0.006f;
                const float lim = 1.55f;
                if (a->pitch >  lim) a->pitch =  lim;
                if (a->pitch < -lim) a->pitch = -lim;
                a->lastX = x; a->lastY = y;
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (a) {
                float d = (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
                a->dist *= (1.0f - d * 0.08f);
                if (a->dist < 1.5f)  a->dist = 1.5f;
                if (a->dist > 12.0f) a->dist = 12.0f;
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// WGL setup
static void loadWglExtensions()
{
    // A dummy window/context is required to query the ARB entry points.
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SurfacesDummyWGL";
    RegisterClassA(&wc);
    HWND dummy = CreateWindowExA(0, wc.lpszClassName, "dummy", 0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(dummy);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;

    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);

    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    g_opengl32 = LoadLibraryA("opengl32.dll");
    loadWglExtensions();

    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SurfacesWindow";
    RegisterClassA(&wc);

    App app;
    RECT r = { 0, 0, 960, 960 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Surfaces",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst, nullptr);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    HDC dc = GetDC(hwnd);

    // Multisampled RGBA pixel format via the ARB path.
    const int pfAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        WGL_SAMPLE_BUFFERS_ARB, 1,
        WGL_SAMPLES_ARB,        4,
        0
    };

    int pf;
	UINT numFormats = 0;

    if (wglChoosePixelFormatARB && wglChoosePixelFormatARB(dc, pfAttribs, nullptr, 1, &pf, &numFormats) && numFormats)
    {
        PIXELFORMATDESCRIPTOR pfd = {}; pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        DescribePixelFormat(dc, pf, sizeof(pfd), &pfd);
        SetPixelFormat(dc, pf, &pfd);
    } 
	else 
		{
        PIXELFORMATDESCRIPTOR pfd = {};
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cDepthBits = 24;
		pfd.cStencilBits = 8;
        int fallback = ChoosePixelFormat(dc, &pfd);
        SetPixelFormat(dc, fallback, &pfd);
    }

    const int ctxAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
    };

    HGLRC rc = wglCreateContextAttribsARB
        ? wglCreateContextAttribsARB(dc, nullptr, ctxAttribs)
        : wglCreateContext(dc);

    wglMakeCurrent(dc, rc);

    if (!gladLoadGL(glLoader)) 
    {
        MessageBoxA(hwnd, "Failed to load OpenGL 3.3 functions.", "Surfaces", MB_ICONERROR);
        return 1;
    }

    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1); // vsync

    GLuint prog = linkProgram();

    glGenVertexArrays(1, &gVAO);
    glGenBuffers(1, &gVBO);
    glGenBuffers(1, &gEBO);
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gEBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    loadSurfaceDefaults(app, SurfType::TorusKnot);
    uploadMesh(app);
    app.dirty = false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glUseProgram(prog);
    GLint uModel   = glGetUniformLocation(prog, "uModel");
    GLint uView    = glGetUniformLocation(prog, "uView");
    GLint uProj    = glGetUniformLocation(prog, "uProj");
    GLint uNMat    = glGetUniformLocation(prog, "uNormalMat");
    GLint uHueAxis = glGetUniformLocation(prog, "uHueAxis");
    GLint uHueScl  = glGetUniformLocation(prog, "uHueScale");
    GLint uTwist   = glGetUniformLocation(prog, "uTwist");
    GLint uGridU   = glGetUniformLocation(prog, "uGridU");
    GLint uGridV   = glGetUniformLocation(prog, "uGridV");
    GLint uWire    = glGetUniformLocation(prog, "uWire");
    glUniform1f(glGetUniformLocation(prog, "uWireStrength"), 0.85f);
    glUniform1f(glGetUniformLocation(prog, "uSat"), 0.95f);
    glUniform1f(glGetUniformLocation(prog, "uVal"), 1.0f);
    glUniform1f(glGetUniformLocation(prog, "uAmbient"), 0.18f);
    glUniform1f(glGetUniformLocation(prog, "uSpecStrength"), 0.7f);
    glUniform1f(glGetUniformLocation(prog, "uShininess"), 48.0f);
    Vec3 ld = normalize(Vec3(0.35f, 0.55f, 0.80f));
    glUniform3f(glGetUniformLocation(prog, "uLightDir"), ld.x, ld.y, ld.z);

    ShowWindow(hwnd, nShow);
    setTitle(hwnd, app);

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (app.running)
    {
        MSG msg;

        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) app.running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (!app.running) break;

        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - prev.QuadPart) / static_cast<float>(freq.QuadPart);
        prev = now;

        if (app.dirty) { uploadMesh(app); app.dirty = false; }
        if (app.autoRotate) app.yaw += dt * 0.35f;

        RECT cr; GetClientRect(hwnd, &cr);
        int fbw = cr.right - cr.left, fbh = cr.bottom - cr.top;
        fbh = std::max(fbh, 1);
        glViewport(0, 0, fbw, fbh);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Mat4 model = mul(rotateY(app.yaw), rotateX(app.pitch));
        Mat4 view  = lookAt(Vec3(0, 0, app.dist), Vec3(0, 0, 0), Vec3(0, 1, 0));
        Mat4 proj  = perspective(45.0f * PI / 180.0f, static_cast<float>(fbw) / static_cast<float>(fbh), 0.1f, 100.0f);
        Mat4 mv    = mul(view, model);

        float nmat[9];
    	mat3from(mv, nmat);

        glUseProgram(prog);
        glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
        glUniformMatrix4fv(uView,  1, GL_FALSE, view.m);
        glUniformMatrix4fv(uProj,  1, GL_FALSE, proj.m);
        glUniformMatrix3fv(uNMat,  1, GL_FALSE, nmat);
        glUniform1i(uHueAxis, app.hueAxis);
        glUniform1f(uHueScl,  app.hueScale);
        glUniform1f(uTwist,   app.twist);
        glUniform1f(uGridU,   app.gridU);
        glUniform1f(uGridV,   app.gridV);
        glUniform1i(uWire,    app.wire ? 1 : 0);

        glBindVertexArray(gVAO);
        glDrawElements(GL_TRIANGLES, app.indexCount, GL_UNSIGNED_INT, 0);

        SwapBuffers(dc);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    return 0;
}
