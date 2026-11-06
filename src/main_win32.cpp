// main_win32.cpp - the window, input and the frame loop. Win32 + WGL, OpenGL 3.3 core.
#include "gl_context.h"
#include "renderer.h"
#include "surfaces.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <string>
#include <string_view>

namespace {

constexpr int   kClientWidth  = 1920;     // the initial client area in 96-dpi units,
constexpr int   kClientHeight = 1080;     // scaled down when it doesn't fit the screen
constexpr float kSpinSpeed    = 0.35f;    // auto-rotation, radians per second
constexpr float kDragSpeed    = 0.006f;   // radians per pixel
constexpr float kMaxPitch     = 1.55f;
constexpr float kMinDistance  = 1.5f;
constexpr float kMaxDistance  = 12.0f;

struct App {
    Surface    surface = Surface::TorusKnot;
    KnotParams knot;
    Style      style;
    Camera     camera;
    bool       autoRotate = true;
    bool       meshStale  = true;

    bool  dragging = false;
    POINT lastMouse{};

    bool            fullscreen = false;
    WINDOWPLACEMENT windowed{};   // restored when leaving fullscreen

    bool running = true;
};

// Switches the surface and resets its colouring; the wireframe toggle stays as it is.
void selectSurface(App& app, Surface surface) {
    const SurfaceInfo info = surfaceInfo(surface);
    app.surface        = surface;
    app.style.hueAxis  = info.hueAxis;
    app.style.hueScale = info.hueScale;
    app.style.twist    = info.twist;
    app.style.gridU    = info.gridU;
    app.style.gridV    = info.gridV;
    app.meshStale      = true;
}

void updateTitle(HWND window, const App& app) {
    const std::string_view name = surfaceInfo(app.surface).name;   // ASCII
    std::wstring title = std::format(L"{}  |  hue={:.1f}  twist={:.2f}", std::wstring(name.begin(), name.end()),
                                     app.style.hueScale, app.style.twist);
    if (app.surface == Surface::TorusKnot)
        title += std::format(L"  p={} q={} tube={:.2f}", app.knot.p, app.knot.q, app.knot.tubeRadius);
    title += L"  |  1-6: surface   G: grid   Space: spin   F11: fullscreen";
    SetWindowTextW(window, title.c_str());
}

// A borderless window over the whole monitor, and back to the saved placement
// (position, size and maximized state).
void toggleFullscreen(HWND window, App& app) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!app.fullscreen) {
        MONITORINFO monitor{};
        monitor.cbSize      = sizeof(monitor);
        app.windowed.length = sizeof(app.windowed);
        if (!GetWindowPlacement(window, &app.windowed) ||
            !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        SetWindowLongPtrW(window, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
        const RECT& area = monitor.rcMonitor;
        SetWindowPos(window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window, &app.windowed);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    app.fullscreen = !app.fullscreen;
}

// Keys by function or position: Esc, Space, F11, letters, digits, arrows.
// Returns whether the title changes.
bool onKeyDown(HWND window, App& app, WPARAM key) {
    switch (key) {
    case VK_ESCAPE:
        app.running = false;
        return false;
    case VK_SPACE:
        app.autoRotate = !app.autoRotate;
        return false;
    case 'G':
        app.style.wireframe = !app.style.wireframe;
        return false;
    case VK_F11:
        toggleFullscreen(window, app);
        return false;
    case 'R':   // view, knot shape and colouring back to the defaults
        app.camera = {};
        app.knot   = {};
        selectSurface(app, app.surface);
        return true;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_UP: {   // torus knot: left/right change p, down/up change q
        if (app.surface != Surface::TorusKnot)
            return false;
        int& turns = (key == VK_LEFT || key == VK_RIGHT) ? app.knot.p : app.knot.q;
        const int step  = (key == VK_RIGHT || key == VK_UP) ? 1 : -1;
        const int value = std::clamp(turns + step, 1, kMaxKnotTurns);
        if (value == turns)
            return false;
        turns         = value;
        app.meshStale = true;
        return true;
    }
    default:
        break;
    }

    // 1 - 6 on the main keyboard or the number pad.
    int digit = -1;
    if (key >= '1' && key <= '9')
        digit = static_cast<int>(key - '1');
    else if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9)
        digit = static_cast<int>(key - VK_NUMPAD1);
    if (digit < 0 || digit >= kSurfaceCount)
        return false;
    selectSurface(app, static_cast<Surface>(digit));
    return true;
}

// Keys by the character they type, so that they work with any keyboard layout.
// Returns whether the title changes.
bool onChar(App& app, wchar_t character) {
    switch (character) {
    case L'[':
        app.style.twist -= 0.05f;
        return true;
    case L']':
        app.style.twist += 0.05f;
        return true;
    case L'-':
        app.style.hueScale = std::max(0.2f, app.style.hueScale - 0.2f);
        return true;
    case L'=':
    case L'+':
        app.style.hueScale += 0.2f;
        return true;
    case L';':
    case L'\'':
        if (app.surface != Surface::TorusKnot)
            return false;
        app.knot.tubeRadius = character == L';' ? std::max(0.05f, app.knot.tubeRadius - 0.05f)
                                                : app.knot.tubeRadius + 0.05f;
        app.meshStale = true;
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {   // CreateWindowExW passes the App along
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CLOSE:   // the frame loop ends, then wWinMain destroys the window
        app->running = false;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (onKeyDown(window, *app, wParam))
            updateTitle(window, *app);
        return 0;
    case WM_CHAR:
        if (onChar(*app, static_cast<wchar_t>(wParam)))
            updateTitle(window, *app);
        return 0;
    case WM_LBUTTONDOWN:
        app->dragging  = true;
        app->lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(window);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();   // WM_CAPTURECHANGED ends the drag
        return 0;
    case WM_CAPTURECHANGED:   // also when another window takes the mouse away
        app->dragging = false;
        return 0;
    case WM_MOUSEMOVE:
        if (app->dragging) {
            const POINT mouse{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            Camera& camera = app->camera;
            camera.yaw  += static_cast<float>(mouse.x - app->lastMouse.x) * kDragSpeed;
            camera.pitch = std::clamp(camera.pitch + static_cast<float>(mouse.y - app->lastMouse.y) * kDragSpeed,
                                      -kMaxPitch, kMaxPitch);
            app->lastMouse = mouse;
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        app->camera.distance = std::clamp(app->camera.distance * (1.0f - notches * 0.08f), kMinDistance, kMaxDistance);
        return 0;
    }
    case WM_DPICHANGED: {   // moved to a monitor with another scale: take the size Windows suggests
        const RECT& suggested = *reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                     suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

// The window for a kClientWidth x kClientHeight client area at the monitor's scale,
// shrunk to fit the work area of the primary monitor and centered on it.
RECT initialWindowRect(DWORD style) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work  = monitor.rcWork;
    const UINT  dpi   = GetDpiForSystem();

    RECT frame{};   // borders and title bar around an empty client area
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
    const int frameWidth  = frame.right - frame.left;
    const int frameHeight = frame.bottom - frame.top;

    const double scale = std::max(0.25, std::min({ dpi / 96.0,
        static_cast<double>(work.right - work.left - frameWidth) / kClientWidth,
        static_cast<double>(work.bottom - work.top - frameHeight) / kClientHeight }));
    const int width  = static_cast<int>(kClientWidth * scale) + frameWidth;
    const int height = static_cast<int>(kClientHeight * scale) + frameHeight;
    const int left   = work.left + (work.right - work.left - width) / 2;
    const int top    = work.top + (work.bottom - work.top - height) / 2;
    return { left, top, left + width, top + height };
}

void runFrameLoop(HWND window, App& app, const GlContext& gl, SurfaceRenderer& renderer) {
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();

    while (app.running) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                app.running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!app.running)
            break;

        RECT client;
        GetClientRect(window, &client);
        if (IsIconic(window) || client.right <= 0 || client.bottom <= 0) {
            WaitMessage();   // nothing to draw: sleep until something happens
            previous = Clock::now();
            continue;
        }

        // Capped, so that the spin doesn't jump after a stall (the window being dragged, say).
        const auto now = Clock::now();
        const float dt = std::min(std::chrono::duration<float>(now - previous).count(), 0.1f);
        previous = now;

        if (app.meshStale) {
            renderer.upload(buildMesh(app.surface, app.knot));
            app.meshStale = false;
        }
        if (app.autoRotate)
            app.camera.yaw += dt * kSpinSpeed;

        renderer.render(client.right, client.bottom, app.camera, app.style);
        gl.swapBuffers();
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Render at the monitor's real resolution instead of being scaled up by Windows.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    App app;
    selectSurface(app, Surface::TorusKnot);

    WNDCLASSW windowClass{};
    windowClass.style         = CS_OWNDC;   // OpenGL keeps using the window's device context
    windowClass.lpfnWndProc   = windowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"SurfacesWindow";
    RegisterClassW(&windowClass);

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    const RECT rect = initialWindowRect(style);
    const HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Surfaces", style, rect.left, rect.top,
                                        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, &app);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the window.", L"Surfaces", MB_ICONERROR);
        return 1;
    }

    int exitCode = 0;
    try {
        const GlContext gl(window);
        SurfaceRenderer renderer;
        ShowWindow(window, showCommand);
        updateTitle(window, app);
        runFrameLoop(window, app, gl, renderer);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Surfaces", MB_ICONERROR);
        exitCode = 1;
    }
    DestroyWindow(window);   // after the renderer and the context are gone
    return exitCode;
}
