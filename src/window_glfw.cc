#ifdef RAFX_BACKEND_GLFW

// clang-format off
#include "rafx_internal.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#if (RAFX_PLATFORM == RAFX_COCOA)
#    include <MetalUtility/MetalUtility.h>
#endif
// clang-format on

//
// Callbacks
//

static void GLFW_ErrorCallback(int32_t error, const char* message) {
    // clang-format off
    printf("[Rafx] GLFW Error [%d]: %s\n", error, message);
#if (RAFX_PLATFORM == RAFX_WINDOWS)
    if (IsDebuggerPresent())
        DebugBreak();
#endif
    // clang-format on
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    if (key < 0 || key >= RFX_MAX_KEYS)
        return;

    if (action == GLFW_PRESS) {
        CORE.Input.keysCurrent[key] = true;
        Input_PushKeyPressed(key);
    } else if (action == GLFW_RELEASE) {
        CORE.Input.keysCurrent[key] = false;
    }
}

static void CharCallback(GLFWwindow* window, unsigned int codepoint) {
    (void)window;
    Input_PushCharPressed((uint32_t)codepoint);
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)window;
    (void)mods;
    if (button < 0 || button >= RFX_MAX_MOUSE_BUTTONS)
        return;
    if (action == GLFW_PRESS)
        CORE.Input.mouseButtonsCurrent[button] = true;
    else if (action == GLFW_RELEASE)
        CORE.Input.mouseButtonsCurrent[button] = false;
}

static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    CORE.Input.mouseX = xpos;
    CORE.Input.mouseY = ypos;
}

static void WindowFocusCallback(GLFWwindow* window, int focused) {
    (void)window;
    CORE.IsFocused = (focused != 0);
}

static void WindowIconifyCallback(GLFWwindow* window, int iconified) {
    (void)window;
    CORE.IsMinimized = (iconified != 0);
}

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    CORE.FramebufferWidth = width;
    CORE.FramebufferHeight = height;
}

//
// Implementation
//

static GLFWcursor* g_Cursors[RFX_CURSOR_COUNT] = { nullptr };

void Backend_EventSleep() {
    glfwWaitEvents();
}

static void ApplyGlfwCreationHints(RfxWindowFlags flags) {
    glfwWindowHint(GLFW_DECORATED, (flags & RFX_WINDOW_BORDERLESS) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, (flags & RFX_WINDOW_NO_RESIZE) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, (flags & RFX_WINDOW_TRANSPARENT) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, (flags & RFX_WINDOW_FLOATING) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, (flags & RFX_WINDOW_MAXIMIZED) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, (flags & RFX_WINDOW_HIDDEN) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, (flags & RFX_WINDOW_SCALE_TO_MONITOR) ? GLFW_TRUE : GLFW_FALSE);
}

static void* GlfwAllocWrapper(size_t size, void* user) {
    (void)user;
    return RfxAlloc(size);
}

static void* GlfwReallocWrapper(void* ptr, size_t size, void* user) {
    (void)user;
    return RfxRealloc(ptr, size);
}

static void GlfwFreeWrapper(void* ptr, void* user) {
    (void)user;
    RfxFree(ptr);
}

bool Backend_CreateWindow(const char* title, int width, int height) {
    glfwSetErrorCallback(GLFW_ErrorCallback);

    GLFWallocator allocator;
    allocator.allocate = GlfwAllocWrapper;
    allocator.reallocate = GlfwReallocWrapper;
    allocator.deallocate = GlfwFreeWrapper;
    allocator.user = nullptr;
    glfwInitAllocator(&allocator);

    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // apply hints
    ApplyGlfwCreationHints(CORE.WindowFlags);

    GLFWmonitor* monitor = nullptr;
    if (CORE.WindowFlags & RFX_WINDOW_FULLSCREEN)
        monitor = glfwGetPrimaryMonitor();

    GLFWwindow* win = glfwCreateWindow(width, height, title, monitor, nullptr);
    if (!win) {
        glfwTerminate();
        return false;
    }
    CORE.WindowHandle = win;

    // handle centering
    if (!monitor && (CORE.WindowFlags & RFX_WINDOW_CENTERED)) {
        GLFWmonitor* prim = glfwGetPrimaryMonitor();
        if (prim) {
            const GLFWvidmode* mode = glfwGetVideoMode(prim);
            int monX, monY;
            glfwGetMonitorPos(prim, &monX, &monY);
            int winW, winH;
            glfwGetWindowSize(win, &winW, &winH);
            glfwSetWindowPos(win, monX + (mode->width - winW) / 2, monY + (mode->height - winH) / 2);
        }
    }

    // init input
    memset(&CORE.Input, 0, sizeof(CORE.Input));
    CORE.Input.firstMouseFrame = true;
    glfwGetCursorPos(win, &CORE.Input.mouseX, &CORE.Input.mouseY);

    // set cbs
    glfwSetKeyCallback(win, KeyCallback);
    glfwSetCharCallback(win, CharCallback);
    glfwSetMouseButtonCallback(win, MouseButtonCallback);
    glfwSetCursorPosCallback(win, CursorPosCallback);
    glfwSetWindowFocusCallback(win, WindowFocusCallback);
    glfwSetWindowIconifyCallback(win, WindowIconifyCallback);
    glfwSetFramebufferSizeCallback(win, FramebufferSizeCallback);

    CORE.VsyncEnable = (CORE.WindowFlags & RFX_WINDOW_VSYNC) != 0;
    CORE.IsFocused = (glfwGetWindowAttrib(win, GLFW_FOCUSED) != 0);
    CORE.IsMinimized = (glfwGetWindowAttrib(win, GLFW_ICONIFIED) != 0);
    glfwGetFramebufferSize(win, &CORE.FramebufferWidth, &CORE.FramebufferHeight);

    if (monitor) {
        CORE.SavedWindowSize[0] = width;
        CORE.SavedWindowSize[1] = height;
        CORE.SavedWindowPos[0] = 100;
        CORE.SavedWindowPos[1] = 100;
    } else {
        glfwGetWindowPos(win, &CORE.SavedWindowPos[0], &CORE.SavedWindowPos[1]);
        glfwGetWindowSize(win, &CORE.SavedWindowSize[0], &CORE.SavedWindowSize[1]);
    }

    return true;
}

void Backend_DestroyWindow() {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (win) {
        glfwDestroyWindow(win);
        CORE.WindowHandle = nullptr;
    }

    // cleanup cursors
    for (int i = 0; i < RFX_CURSOR_COUNT; i++) {
        if (g_Cursors[i]) {
            glfwDestroyCursor(g_Cursors[i]);
            g_Cursors[i] = nullptr;
        }
    }

    glfwTerminate();
}

void Backend_SetWindowFlags(RfxWindowFlags flags) {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (!win) {
        CORE.WindowFlags = flags;
        return;
    }

    RfxWindowFlags old = CORE.WindowFlags;
    if (flags == old)
        return;
    CORE.WindowFlags = flags;

    // borderless
    glfwSetWindowAttrib(win, GLFW_DECORATED, (flags & RFX_WINDOW_BORDERLESS) ? GLFW_FALSE : GLFW_TRUE);

    // resize
    glfwSetWindowAttrib(win, GLFW_RESIZABLE, (flags & RFX_WINDOW_NO_RESIZE) ? GLFW_FALSE : GLFW_TRUE);

    // floating
    glfwSetWindowAttrib(win, GLFW_FLOATING, (flags & RFX_WINDOW_FLOATING) ? GLFW_TRUE : GLFW_FALSE);

    // vis
    if (flags & RFX_WINDOW_HIDDEN)
        glfwHideWindow(win);
    else
        glfwShowWindow(win);

    // maximized
    bool isMax = glfwGetWindowAttrib(win, GLFW_MAXIMIZED);
    bool wantMax = (flags & RFX_WINDOW_MAXIMIZED);
    if (wantMax && !isMax)
        glfwMaximizeWindow(win);
    else if (!wantMax && isMax)
        glfwRestoreWindow(win);

    // fullscreen toggle
    bool fs = (flags & RFX_WINDOW_FULLSCREEN);
    bool oldFs = (old & RFX_WINDOW_FULLSCREEN);

    if (fs != oldFs) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        if (fs) {
            glfwGetWindowPos(win, &CORE.SavedWindowPos[0], &CORE.SavedWindowPos[1]);
            glfwGetWindowSize(win, &CORE.SavedWindowSize[0], &CORE.SavedWindowSize[1]);
            glfwSetWindowMonitor(win, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            // restore saved state
            glfwSetWindowMonitor(
                win, nullptr, CORE.SavedWindowPos[0], CORE.SavedWindowPos[1], CORE.SavedWindowSize[0], CORE.SavedWindowSize[1], 0
            );
            glfwSetWindowAttrib(win, GLFW_DECORATED, (flags & RFX_WINDOW_BORDERLESS) ? GLFW_FALSE : GLFW_TRUE);
            glfwSetWindowAttrib(win, GLFW_FLOATING, (flags & RFX_WINDOW_FLOATING) ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    bool vsync = (flags & RFX_WINDOW_VSYNC);
    if (vsync != CORE.VsyncEnable) {
        CORE.VsyncEnable = vsync;
        CORE.SwapChainWidth = 0; // trigger recreation
    }
}

bool Backend_WindowShouldClose() {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    return win ? glfwWindowShouldClose(win) : true;
}

void Backend_PollEvents() {
    glfwPollEvents();
}

void Backend_GetWindowSize(int* width, int* height) {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (win)
        glfwGetWindowSize(win, width, height);
}

int Backend_GetWindowWidth() {
    int w;
    Backend_GetWindowSize(&w, nullptr);
    return w;
}

int Backend_GetWindowHeight() {
    int h;
    Backend_GetWindowSize(nullptr, &h);
    return h;
}

double Backend_GetTime() {
    return glfwGetTime();
}

void Backend_SetMouseCursorVisible(bool visible) {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (win) {
        glfwSetInputMode(win, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        CORE.Input.firstMouseFrame = true;
    }
}

void Backend_SetMouseCursor(RfxCursorType cursor) {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (!win)
        return;

    if (cursor == RFX_CURSOR_DEFAULT) {
        glfwSetCursor(win, NULL);
        return;
    }

    if (cursor >= RFX_CURSOR_COUNT || cursor < 0)
        return;

    int shape = GLFW_ARROW_CURSOR;
    switch (cursor) {
    case RFX_CURSOR_ARROW: shape = GLFW_ARROW_CURSOR; break;
    case RFX_CURSOR_IBEAM: shape = GLFW_IBEAM_CURSOR; break;
    case RFX_CURSOR_CROSSHAIR: shape = GLFW_CROSSHAIR_CURSOR; break;
    case RFX_CURSOR_HAND: shape = GLFW_POINTING_HAND_CURSOR; break;
    case RFX_CURSOR_RESIZE_EW:
    case RFX_CURSOR_RESIZE_E:
    case RFX_CURSOR_RESIZE_W: shape = GLFW_RESIZE_EW_CURSOR; break;
    case RFX_CURSOR_RESIZE_NS:
    case RFX_CURSOR_RESIZE_N:
    case RFX_CURSOR_RESIZE_S: shape = GLFW_RESIZE_NS_CURSOR; break;
    case RFX_CURSOR_RESIZE_NWSE:
    case RFX_CURSOR_RESIZE_NW:
    case RFX_CURSOR_RESIZE_SE: shape = GLFW_RESIZE_NWSE_CURSOR; break;
    case RFX_CURSOR_RESIZE_NESW:
    case RFX_CURSOR_RESIZE_NE:
    case RFX_CURSOR_RESIZE_SW: shape = GLFW_RESIZE_NESW_CURSOR; break;
    case RFX_CURSOR_RESIZE_ALL: shape = GLFW_RESIZE_ALL_CURSOR; break;
    case RFX_CURSOR_NOT_ALLOWED: shape = GLFW_NOT_ALLOWED_CURSOR; break;
    case RFX_CURSOR_WAIT:
    case RFX_CURSOR_PROGRESS: shape = GLFW_ARROW_CURSOR; break;
    default: break;
    }

    if (!g_Cursors[cursor]) {
        g_Cursors[cursor] = glfwCreateStandardCursor(shape);
    }

    glfwSetCursor(win, g_Cursors[cursor]);
}

void Backend_GetNativeHandles(nri::Window& nriWindow) {
    GLFWwindow* win = (GLFWwindow*)CORE.WindowHandle;
    if (!win)
        return;

    // clang-format off
#if (RAFX_PLATFORM == RAFX_WINDOWS)
    nriWindow.windows.hwnd = glfwGetWin32Window(win);
#elif (RAFX_PLATFORM == RAFX_WAYLAND)
    nriWindow.wayland.display = glfwGetWaylandDisplay();
    nriWindow.wayland.surface = glfwGetWaylandWindow(win);
#elif (RAFX_PLATFORM == RAFX_X11)
    nriWindow.x11.dpy = glfwGetX11Display();
    nriWindow.x11.window = glfwGetX11Window(win);
#elif (RAFX_PLATFORM == RAFX_COCOA)
    nriWindow.metal.caMetalLayer = GetMetalLayer(win);
#endif
    // clang-format on
}

#endif // RAFX_BACKEND_GLFW
