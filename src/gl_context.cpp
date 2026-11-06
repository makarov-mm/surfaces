#include "gl_context.h"

#include <glad/gl.h>

#include <cstdint>
#include <stdexcept>

namespace {

// WGL_ARB_pixel_format, WGL_ARB_multisample, WGL_ARB_create_context(_profile).
constexpr int WGL_DRAW_TO_WINDOW_ARB                 = 0x2001;
constexpr int WGL_ACCELERATION_ARB                   = 0x2003;
constexpr int WGL_SUPPORT_OPENGL_ARB                 = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB                  = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB                     = 0x2013;
constexpr int WGL_COLOR_BITS_ARB                     = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB                     = 0x2022;
constexpr int WGL_STENCIL_BITS_ARB                   = 0x2023;
constexpr int WGL_FULL_ACCELERATION_ARB              = 0x2027;
constexpr int WGL_TYPE_RGBA_ARB                      = 0x202B;
constexpr int WGL_SAMPLE_BUFFERS_ARB                 = 0x2041;
constexpr int WGL_SAMPLES_ARB                        = 0x2042;
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB          = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB          = 0x2092;
constexpr int WGL_CONTEXT_FLAGS_ARB                  = 0x2094;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB           = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB       = 0x0001;
constexpr int WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB = 0x0002;

using CreateContextAttribsArb = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using ChoosePixelFormatArb    = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using SwapIntervalExt         = BOOL(WINAPI*)(int);

struct WglExtensions {
    CreateContextAttribsArb createContextAttribs = nullptr;
    ChoosePixelFormatArb    choosePixelFormat    = nullptr;
};

template <typename Function>
Function wglFunction(const char* name) {
    // Cast through void (*)(), the generic function pointer type: compilers flag a direct cast from PROC.
    return reinterpret_cast<Function>(reinterpret_cast<void (*)()>(wglGetProcAddress(name)));
}

PIXELFORMATDESCRIPTOR basicPixelFormat() {
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    return pfd;
}

constexpr const wchar_t* kThrowawayClass = L"SurfacesDummyWGL";

// The WGL extension functions can only be looked up with a context current, and a
// window's pixel format can be set only once, so this takes a throwaway window
// with a legacy context.
WglExtensions loadWglExtensions() {
    struct Throwaway {   // released in reverse order of creation
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND      window   = nullptr;
        HDC       dc       = nullptr;
        HGLRC     context  = nullptr;

        ~Throwaway() {
            if (context) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(context);
            }
            if (dc)
                ReleaseDC(window, dc);
            if (window)
                DestroyWindow(window);
            UnregisterClassW(kThrowawayClass, instance);
        }
    } throwaway;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc   = DefWindowProcW;
    windowClass.hInstance     = throwaway.instance;
    windowClass.lpszClassName = kThrowawayClass;
    RegisterClassW(&windowClass);
    throwaway.window = CreateWindowExW(0, kThrowawayClass, L"", 0, 0, 0, 1, 1, nullptr, nullptr, throwaway.instance, nullptr);
    if (!throwaway.window)
        return {};
    throwaway.dc = GetDC(throwaway.window);
    if (!throwaway.dc)
        return {};

    const PIXELFORMATDESCRIPTOR pfd = basicPixelFormat();
    if (!SetPixelFormat(throwaway.dc, ChoosePixelFormat(throwaway.dc, &pfd), &pfd))
        return {};
    throwaway.context = wglCreateContext(throwaway.dc);
    if (!throwaway.context || !wglMakeCurrent(throwaway.dc, throwaway.context))
        return {};

    return { .createContextAttribs = wglFunction<CreateContextAttribsArb>("wglCreateContextAttribsARB"),
             .choosePixelFormat    = wglFunction<ChoosePixelFormatArb>("wglChoosePixelFormatARB") };
}

// RGBA with 4x MSAA through WGL_ARB_pixel_format; without the extension or such a
// format, the basic format without multisampling.
void setPixelFormat(HDC dc, const WglExtensions& wgl) {
    if (wgl.choosePixelFormat) {
        const int attributes[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
            WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB,     32,
            WGL_DEPTH_BITS_ARB,     24,
            WGL_STENCIL_BITS_ARB,   8,
            WGL_SAMPLE_BUFFERS_ARB, 1,
            WGL_SAMPLES_ARB,        4,
            0
        };
        int  format = 0;
        UINT count  = 0;
        if (wgl.choosePixelFormat(dc, attributes, nullptr, 1, &format, &count) && count > 0) {
            PIXELFORMATDESCRIPTOR pfd{};
            DescribePixelFormat(dc, format, sizeof(pfd), &pfd);
            if (SetPixelFormat(dc, format, &pfd))
                return;
        }
    }
    const PIXELFORMATDESCRIPTOR pfd = basicPixelFormat();
    if (!SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd))
        throw std::runtime_error("No pixel format for OpenGL is available.");
}

HGLRC createContext(HDC dc, const WglExtensions& wgl) {
    if (!wgl.createContextAttribs)   // very old driver: take its default context, the version check decides
        return wglCreateContext(dc);
    const int attributes[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
    };
    return wgl.createContextAttribs(dc, nullptr, attributes);
}

// The loader for glad: wglGetProcAddress has the functions newer than OpenGL 1.1,
// opengl32.dll exports the rest.
GLADapiproc loadGlFunction(const char* name) {
    const PROC proc = wglGetProcAddress(name);
    // Some drivers return small sentinel values instead of null.
    const auto value = reinterpret_cast<std::intptr_t>(proc);
    if (value != 0 && value != 1 && value != 2 && value != 3 && value != -1)
        return reinterpret_cast<GLADapiproc>(proc);
    static const HMODULE opengl32 = GetModuleHandleW(L"opengl32.dll");
    return reinterpret_cast<GLADapiproc>(GetProcAddress(opengl32, name));
}

} // namespace

GlContext::GlContext(HWND window)
    : m_window(window) {
    try {
        const WglExtensions wgl = loadWglExtensions();

        m_dc = GetDC(window);
        if (!m_dc)
            throw std::runtime_error("The window has no device context.");
        setPixelFormat(m_dc, wgl);

        m_context = createContext(m_dc, wgl);
        if (!m_context || !wglMakeCurrent(m_dc, m_context))
            throw std::runtime_error("Could not create an OpenGL 3.3 core context.");
        if (gladLoadGL(loadGlFunction) < GLAD_MAKE_VERSION(3, 3))
            throw std::runtime_error("The graphics driver does not support OpenGL 3.3.");

        // Vsync. The function belongs to the context, so it is looked up only now.
        if (const auto swapInterval = wglFunction<SwapIntervalExt>("wglSwapIntervalEXT"))
            swapInterval(1);
    } catch (...) {
        release();
        throw;
    }
}

GlContext::~GlContext() {
    release();
}

void GlContext::swapBuffers() const {
    SwapBuffers(m_dc);
}

void GlContext::release() {
    if (m_context) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_context);
        m_context = nullptr;
    }
    if (m_dc) {
        ReleaseDC(m_window, m_dc);
        m_dc = nullptr;
    }
}
