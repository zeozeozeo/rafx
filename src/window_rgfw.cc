#ifdef RAFX_BACKEND_RGFW

// clang-format off
#include "rafx_internal.h"

// RGFW will define it
#if (RAFX_PLATFORM == RAFX_WINDOWS) && defined(WIN32_LEAN_AND_MEAN)
#    undef WIN32_LEAN_AND_MEAN
#endif

#define RGFW_ALLOC RfxAlloc
#define RGFW_FREE RfxFree

#define RGFW_IMPLEMENTATION
#include <RGFW.h>

#include <chrono>

// clang-format on

static std::chrono::time_point<std::chrono::high_resolution_clock> g_StartTime;

static int MapRGFWKey(u32 key) {
    // ascii fastpath
    if (key >= '0' && key <= '9')
        return key;
    if (key >= 'a' && key <= 'z')
        return key - 32; // RGFW is lowercase, GLFW is uppercase
    if (key >= 'A' && key <= 'Z')
        return key;

    switch (key) {
    case RGFW_space: return RFX_KEY_SPACE;
    case RGFW_apostrophe: return RFX_KEY_APOSTROPHE;
    case RGFW_comma: return RFX_KEY_COMMA;
    case RGFW_minus: return RFX_KEY_MINUS;
    case RGFW_period: return RFX_KEY_PERIOD;
    case RGFW_slash: return RFX_KEY_SLASH;
    case RGFW_semicolon: return RFX_KEY_SEMICOLON;
    case RGFW_equal: return RFX_KEY_EQUAL;
    case RGFW_bracket: return RFX_KEY_LEFT_BRACKET;
    case RGFW_backSlash: return RFX_KEY_BACKSLASH;
    case RGFW_closeBracket: return RFX_KEY_RIGHT_BRACKET;
    case RGFW_backtick: return RFX_KEY_GRAVE_ACCENT;
    case RGFW_escape: return RFX_KEY_ESCAPE;
    case RGFW_return: return RFX_KEY_ENTER;
    case RGFW_tab: return RFX_KEY_TAB;
    case RGFW_backSpace: return RFX_KEY_BACKSPACE;
    case RGFW_insert: return RFX_KEY_INSERT;
    case RGFW_delete: return RFX_KEY_DELETE;
    case RGFW_right: return RFX_KEY_RIGHT;
    case RGFW_left: return RFX_KEY_LEFT;
    case RGFW_down: return RFX_KEY_DOWN;
    case RGFW_up: return RFX_KEY_UP;
    case RGFW_pageUp: return RFX_KEY_PAGE_UP;
    case RGFW_pageDown: return RFX_KEY_PAGE_DOWN;
    case RGFW_home: return RFX_KEY_HOME;
    case RGFW_end: return RFX_KEY_END;
    case RGFW_capsLock: return RFX_KEY_CAPS_LOCK;
    case RGFW_scrollLock: return RFX_KEY_SCROLL_LOCK;
    case RGFW_numLock: return RFX_KEY_NUM_LOCK;
    case RGFW_printScreen: return RFX_KEY_PRINT_SCREEN;
    case RGFW_pause: return RFX_KEY_PAUSE;
    case RGFW_F1: return RFX_KEY_F1;
    case RGFW_F2: return RFX_KEY_F2;
    case RGFW_F3: return RFX_KEY_F3;
    case RGFW_F4: return RFX_KEY_F4;
    case RGFW_F5: return RFX_KEY_F5;
    case RGFW_F6: return RFX_KEY_F6;
    case RGFW_F7: return RFX_KEY_F7;
    case RGFW_F8: return RFX_KEY_F8;
    case RGFW_F9: return RFX_KEY_F9;
    case RGFW_F10: return RFX_KEY_F10;
    case RGFW_F11: return RFX_KEY_F11;
    case RGFW_F12: return RFX_KEY_F12;
    case RGFW_shiftL: return RFX_KEY_LEFT_SHIFT;
    case RGFW_controlL: return RFX_KEY_LEFT_CONTROL;
    case RGFW_altL: return RFX_KEY_LEFT_ALT;
    case RGFW_superL: return RFX_KEY_LEFT_SUPER;
    case RGFW_shiftR: return RFX_KEY_RIGHT_SHIFT;
    case RGFW_controlR: return RFX_KEY_RIGHT_CONTROL;
    case RGFW_altR: return RFX_KEY_RIGHT_ALT;
    case RGFW_superR: return RFX_KEY_RIGHT_SUPER;
    case RGFW_menu: return RFX_KEY_MENU;
    default: return -1;
    }
}

static u32 MapRfxFlagsToRGFW(RfxWindowFlags flags) {
    u32 r = 0;
    if (flags & RFX_WINDOW_BORDERLESS)
        r |= RGFW_windowNoBorder;
    if (flags & RFX_WINDOW_FULLSCREEN)
        r |= RGFW_windowFullscreen;
    if (flags & RFX_WINDOW_NO_RESIZE)
        r |= RGFW_windowNoResize;
    if (flags & RFX_WINDOW_TRANSPARENT)
        r |= RGFW_windowTransparent;
    if (flags & RFX_WINDOW_FLOATING)
        r |= RGFW_windowFloating;
    if (flags & RFX_WINDOW_MAXIMIZED)
        r |= RGFW_windowMaximize;
    if (flags & RFX_WINDOW_HIDDEN)
        r |= RGFW_windowHide;
    if (flags & RFX_WINDOW_CENTERED)
        r |= RGFW_windowCenter;
    if ((flags & RFX_WINDOW_NO_SCALE) == 0)
        r |= RGFW_windowScaleToMonitor;
    return r;
}

bool Backend_CreateWindow(const char* title, int width, int height) {
    RGFW_windowFlags flags = MapRfxFlagsToRGFW(CORE.WindowFlags);

    RGFW_window* win = RGFW_createWindow(title, 0, 0, width, height, flags);
    if (!win)
        return false;

    CORE.WindowHandle = win;
    g_StartTime = std::chrono::high_resolution_clock::now();

    CORE.Input.firstMouseFrame = true;
    int x, y;
    RGFW_window_getMouse(win, &x, &y);
    CORE.Input.mouseX = x;
    CORE.Input.mouseY = y;

    CORE.FramebufferWidth = win->w;
    CORE.FramebufferHeight = win->h;

    CORE.SavedWindowPos[0] = win->x;
    CORE.SavedWindowPos[1] = win->y;
    CORE.SavedWindowSize[0] = win->w;
    CORE.SavedWindowSize[1] = win->h;

    return true;
}

void Backend_DestroyWindow() {
    if (CORE.WindowHandle) {
        RGFW_window_close((RGFW_window*)CORE.WindowHandle);
        CORE.WindowHandle = nullptr;
    }
}

void Backend_SetWindowFlags(RfxWindowFlags flags) {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (!win) {
        CORE.WindowFlags = flags;
        return;
    }

    RfxWindowFlags old = CORE.WindowFlags;
    if (flags == old)
        return;
    CORE.WindowFlags = flags;

    u32 rgfwFlags = MapRfxFlagsToRGFW(flags);
    RGFW_window_setFlags(win, rgfwFlags);

    bool vsync = (flags & RFX_WINDOW_VSYNC);
    if (vsync != CORE.VsyncEnable) {
        CORE.VsyncEnable = vsync;
        CORE.SwapChainWidth = 0; // trigger recreation
    }
}

bool Backend_WindowShouldClose() {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    return win ? RGFW_window_shouldClose(win) : true;
}

void Backend_PollEvents() {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (!win)
        return;

    RGFW_event event;
    while (RGFW_window_checkEvent(win, &event)) {
        if (event.type == RGFW_quit)
            break;

        if (event.type == RGFW_keyPressed || event.type == RGFW_keyReleased) {
            int mapped = MapRGFWKey(event.key.value);
            if (mapped >= 0 && mapped < RFX_MAX_KEYS) {
                CORE.Input.keysCurrent[mapped] = (event.type == RGFW_keyPressed);
                if (event.type == RGFW_keyPressed && !event.key.repeat) {
                    Input_PushKeyPressed(mapped);
                }
            }
            // HUGE TODO: RGFW doesn't do unicode, but I want it
            if (event.type == RGFW_keyPressed) {
                if (event.key.sym != 0) {
                    Input_PushCharPressed((uint32_t)event.key.sym);
                }
            }
        } else if (event.type == RGFW_mouseButtonPressed || event.type == RGFW_mouseButtonReleased) {
            int btn = -1;
            if (event.button.value == RGFW_mouseLeft)
                btn = RFX_MOUSE_BUTTON_LEFT;
            else if (event.button.value == RGFW_mouseRight)
                btn = RFX_MOUSE_BUTTON_RIGHT;
            else if (event.button.value == RGFW_mouseMiddle)
                btn = RFX_MOUSE_BUTTON_MIDDLE;

            if (btn >= 0 && btn < RFX_MAX_MOUSE_BUTTONS) {
                CORE.Input.mouseButtonsCurrent[btn] = (event.type == RGFW_mouseButtonPressed);
            }
        } else if (event.type == RGFW_mousePosChanged) {
            CORE.Input.mouseX = event.mouse.x;
            CORE.Input.mouseY = event.mouse.y;
        } else if (true) {
        } else if (event.type == RGFW_windowResized) {
            CORE.FramebufferWidth = win->w;
            CORE.FramebufferHeight = win->h;
        } else if (event.type == RGFW_focusIn) {
            CORE.IsFocused = true;
        } else if (event.type == RGFW_focusOut) {
            CORE.IsFocused = false;
        } else if (event.type == RGFW_windowMinimized) {
            CORE.IsMinimized = true;
        } else if (event.type == RGFW_windowRestored) {
            CORE.IsMinimized = false;
        }
    }
}

void Backend_GetWindowSize(int* width, int* height) {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (width)
        *width = win ? win->w : 0;
    if (height)
        *height = win ? win->h : 0;
}

int Backend_GetWindowWidth() {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    return win ? win->w : 0;
}

int Backend_GetWindowHeight() {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    return win ? win->h : 0;
}

float Backend_GetWindowScale() {
    // TODO
    return 1.0f;
}

double Backend_GetTime() {
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - g_StartTime;
    return elapsed.count();
}

void Backend_SetMouseCursorVisible(bool visible) {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (win) {
        RGFW_window_showMouse(win, visible ? RGFW_TRUE : RGFW_FALSE);
        if (!visible) {
            RGFW_window_setRawMouseMode(win, RGFW_TRUE);
            CORE.Input.firstMouseFrame = true;
        } else {
            RGFW_window_setRawMouseMode(win, RGFW_FALSE);
        }
    }
}

void Backend_GetNativeHandles(nri::Window& nriWindow) {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (!win)
        return;

    // clang-format off
#if (RAFX_PLATFORM == RAFX_WINDOWS)
    nriWindow.windows.hwnd = RGFW_window_getHWND(win);
#elif (RAFX_PLATFORM == RAFX_WAYLAND)
    nriWindow.wayland.display = RGFW_getDisplay_Wayland();
    nriWindow.wayland.surface = RGFW_window_getWindow_Wayland(win);
#elif (RAFX_PLATFORM == RAFX_X11)
    nriWindow.x11.dpy = RGFW_getDisplay_X11();
    nriWindow.x11.window = (uint64_t)(uintptr_t)RGFW_window_getWindow_X11(win);
#elif (RAFX_PLATFORM == RAFX_COCOA)
    nriWindow.metal.caMetalLayer = RGFW_getLayer_OSX();
#endif
    // clang-format on
}

void Backend_EventSleep() {
    RGFW_waitForEvent(100);
}

void Backend_SetMouseCursor(RfxCursorType cursor) {
    RGFW_window* win = (RGFW_window*)CORE.WindowHandle;
    if (!win)
        return;

    if (cursor == RFX_CURSOR_DEFAULT) {
        RGFW_window_setMouseDefault(win);
        return;
    }

    if (cursor < 0 || cursor >= RFX_CURSOR_COUNT)
        return;

    RGFW_mouseIcons icon = RGFW_mouseNormal;
    switch (cursor) {
    case RFX_CURSOR_ARROW: icon = RGFW_mouseArrow; break;
    case RFX_CURSOR_IBEAM: icon = RGFW_mouseIbeam; break;
    case RFX_CURSOR_CROSSHAIR: icon = RGFW_mouseCrosshair; break;
    case RFX_CURSOR_HAND: icon = RGFW_mousePointingHand; break;
    case RFX_CURSOR_RESIZE_EW: icon = RGFW_mouseResizeEW; break;
    case RFX_CURSOR_RESIZE_NS: icon = RGFW_mouseResizeNS; break;
    case RFX_CURSOR_RESIZE_NWSE: icon = RGFW_mouseResizeNWSE; break;
    case RFX_CURSOR_RESIZE_NESW: icon = RGFW_mouseResizeNESW; break;
    case RFX_CURSOR_RESIZE_ALL: icon = RGFW_mouseResizeAll; break;
    case RFX_CURSOR_NOT_ALLOWED: icon = RGFW_mouseNotAllowed; break;
    case RFX_CURSOR_RESIZE_NW: icon = RGFW_mouseResizeNW; break;
    case RFX_CURSOR_RESIZE_N: icon = RGFW_mouseResizeN; break;
    case RFX_CURSOR_RESIZE_NE: icon = RGFW_mouseResizeNE; break;
    case RFX_CURSOR_RESIZE_E: icon = RGFW_mouseResizeE; break;
    case RFX_CURSOR_RESIZE_SE: icon = RGFW_mouseResizeSE; break;
    case RFX_CURSOR_RESIZE_S: icon = RGFW_mouseResizeS; break;
    case RFX_CURSOR_RESIZE_SW: icon = RGFW_mouseResizeSW; break;
    case RFX_CURSOR_RESIZE_W: icon = RGFW_mouseResizeW; break;
    case RFX_CURSOR_WAIT: icon = RGFW_mouseWait; break;
    case RFX_CURSOR_PROGRESS: icon = RGFW_mouseProgress; break;
    default: break;
    }
    RGFW_window_setMouseStandard(win, icon);
}

#endif // RAFX_BACKEND_RGFW
