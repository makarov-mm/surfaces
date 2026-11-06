// gl_context.h - an OpenGL 3.3 core context on a Win32 window (WGL).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Sets up the window's pixel format (4x MSAA when the driver offers it), creates the
// context, makes it current, loads the GL functions and turns on vsync.
// Throws std::runtime_error when OpenGL 3.3 is not available.
class GlContext {
public:
    explicit GlContext(HWND window);
    ~GlContext();
    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    void swapBuffers() const;

private:
    void release();

    HWND  m_window  = nullptr;
    HDC   m_dc      = nullptr;
    HGLRC m_context = nullptr;
};
