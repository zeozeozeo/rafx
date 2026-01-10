#ifdef RAFX_BACKEND_SDL

// clang-format off
#include "rafx_internal.h"
#include <SDL3/SDL.h>
// clang-format on

//
// Globals
//

static SDL_Cursor* g_Cursors[RFX_CURSOR_COUNT] = { nullptr };
static bool g_ShouldClose = false;
static uint64_t g_Frequency = 0;

//
// Utils
//

static int MapSDLKey(SDL_Keycode key) {
    if (key >= '0' && key <= '9')
        return key;
    if (key >= 'a' && key <= 'z')
        return key - 32;
    if (key >= 'A' && key <= 'Z')
        return key;

    switch (key) {
    case SDLK_SPACE: return RFX_KEY_SPACE;
    case SDLK_APOSTROPHE: return RFX_KEY_APOSTROPHE;
    case SDLK_COMMA: return RFX_KEY_COMMA;
    case SDLK_MINUS: return RFX_KEY_MINUS;
    case SDLK_PERIOD: return RFX_KEY_PERIOD;
    case SDLK_SLASH: return RFX_KEY_SLASH;
    case SDLK_SEMICOLON: return RFX_KEY_SEMICOLON;
    case SDLK_EQUALS: return RFX_KEY_EQUAL;
    case SDLK_LEFTBRACKET: return RFX_KEY_LEFT_BRACKET;
    case SDLK_BACKSLASH: return RFX_KEY_BACKSLASH;
    case SDLK_RIGHTBRACKET: return RFX_KEY_RIGHT_BRACKET;
    case SDLK_GRAVE: return RFX_KEY_GRAVE_ACCENT;
    case SDLK_ESCAPE: return RFX_KEY_ESCAPE;
    case SDLK_RETURN: return RFX_KEY_ENTER;
    case SDLK_TAB: return RFX_KEY_TAB;
    case SDLK_BACKSPACE: return RFX_KEY_BACKSPACE;
    case SDLK_INSERT: return RFX_KEY_INSERT;
    case SDLK_DELETE: return RFX_KEY_DELETE;
    case SDLK_RIGHT: return RFX_KEY_RIGHT;
    case SDLK_LEFT: return RFX_KEY_LEFT;
    case SDLK_DOWN: return RFX_KEY_DOWN;
    case SDLK_UP: return RFX_KEY_UP;
    case SDLK_PAGEUP: return RFX_KEY_PAGE_UP;
    case SDLK_PAGEDOWN: return RFX_KEY_PAGE_DOWN;
    case SDLK_HOME: return RFX_KEY_HOME;
    case SDLK_END: return RFX_KEY_END;
    case SDLK_CAPSLOCK: return RFX_KEY_CAPS_LOCK;
    case SDLK_SCROLLLOCK: return RFX_KEY_SCROLL_LOCK;
    case SDLK_NUMLOCKCLEAR: return RFX_KEY_NUM_LOCK;
    case SDLK_PRINTSCREEN: return RFX_KEY_PRINT_SCREEN;
    case SDLK_PAUSE: return RFX_KEY_PAUSE;
    case SDLK_F1: return RFX_KEY_F1;
    case SDLK_F2: return RFX_KEY_F2;
    case SDLK_F3: return RFX_KEY_F3;
    case SDLK_F4: return RFX_KEY_F4;
    case SDLK_F5: return RFX_KEY_F5;
    case SDLK_F6: return RFX_KEY_F6;
    case SDLK_F7: return RFX_KEY_F7;
    case SDLK_F8: return RFX_KEY_F8;
    case SDLK_F9: return RFX_KEY_F9;
    case SDLK_F10: return RFX_KEY_F10;
    case SDLK_F11: return RFX_KEY_F11;
    case SDLK_F12: return RFX_KEY_F12;
    case SDLK_LSHIFT: return RFX_KEY_LEFT_SHIFT;
    case SDLK_LCTRL: return RFX_KEY_LEFT_CONTROL;
    case SDLK_LALT: return RFX_KEY_LEFT_ALT;
    case SDLK_LGUI: return RFX_KEY_LEFT_SUPER;
    case SDLK_RSHIFT: return RFX_KEY_RIGHT_SHIFT;
    case SDLK_RCTRL: return RFX_KEY_RIGHT_CONTROL;
    case SDLK_RALT: return RFX_KEY_RIGHT_ALT;
    case SDLK_RGUI: return RFX_KEY_RIGHT_SUPER;
    case SDLK_MENU: return RFX_KEY_MENU;
    default: return -1;
    }
}

//
// Implementation
//

void Backend_EventSleep() {
    SDL_WaitEvent(NULL);
}

static void* SDLCALL SdlAllocWrapper(size_t size) {
    return RfxAlloc(size);
}

static void* SDLCALL SdlCallocWrapper(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = RfxAlloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

static void* SDLCALL SdlReallocWrapper(void* mem, size_t size) {
    return RfxRealloc(mem, size);
}

static void SDLCALL SdlFreeWrapper(void* mem) {
    RfxFree(mem);
}

bool Backend_CreateWindow(const char* title, int width, int height) {
    SDL_SetMemoryFunctions(SdlAllocWrapper, SdlCallocWrapper, SdlReallocWrapper, SdlFreeWrapper);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("[Rafx] SDL_Init Failed: %s\n", SDL_GetError());
        return false;
    }

    g_Frequency = SDL_GetPerformanceFrequency();

    SDL_WindowFlags sdlFlags = 0;

    // map flags
    if (CORE.WindowFlags & RFX_WINDOW_BORDERLESS)
        sdlFlags |= SDL_WINDOW_BORDERLESS;
    if (CORE.WindowFlags & RFX_WINDOW_FULLSCREEN)
        sdlFlags |= SDL_WINDOW_FULLSCREEN;
    if ((CORE.WindowFlags & RFX_WINDOW_NO_RESIZE) == 0)
        sdlFlags |= SDL_WINDOW_RESIZABLE;
    if (CORE.WindowFlags & RFX_WINDOW_TRANSPARENT)
        sdlFlags |= SDL_WINDOW_TRANSPARENT;
    if (CORE.WindowFlags & RFX_WINDOW_FLOATING)
        sdlFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
    if (CORE.WindowFlags & RFX_WINDOW_MAXIMIZED)
        sdlFlags |= SDL_WINDOW_MAXIMIZED;
    if (CORE.WindowFlags & RFX_WINDOW_HIDDEN)
        sdlFlags |= SDL_WINDOW_HIDDEN;
    if ((CORE.WindowFlags & RFX_WINDOW_NO_SCALE) == 0)
        sdlFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window* win = SDL_CreateWindow(title, width, height, sdlFlags);
    if (!win) {
        printf("[Rafx] SDL3 SDL_CreateWindow Failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    CORE.WindowHandle = win;
    g_ShouldClose = false;

    // TODO: Should we have this on always? Probably not. At least not on mobile. I don't know.
    //       https://github.com/libsdl-org/SDL/issues/9309
    SDL_StartTextInput(win);

    // center
    if (CORE.WindowFlags & RFX_WINDOW_CENTERED) {
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    // init input
    memset(&CORE.Input, 0, sizeof(CORE.Input));
    CORE.Input.firstMouseFrame = true;
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    CORE.Input.mouseX = (double)mx;
    CORE.Input.mouseY = (double)my;

    int fw, fh;
    SDL_GetWindowSizeInPixels(win, &fw, &fh);
    CORE.FramebufferWidth = fw;
    CORE.FramebufferHeight = fh;

    // save initial state
    int x, y, w, h;
    SDL_GetWindowPosition(win, &x, &y);
    SDL_GetWindowSize(win, &w, &h);
    CORE.SavedWindowPos[0] = x;
    CORE.SavedWindowPos[1] = y;
    CORE.SavedWindowSize[0] = w;
    CORE.SavedWindowSize[1] = h;

    CORE.IsFocused = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
    CORE.IsMinimized = (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) != 0;
    CORE.VsyncEnable = (CORE.WindowFlags & RFX_WINDOW_VSYNC) != 0;

    return true;
}

void Backend_DestroyWindow() {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (win) {
        SDL_StopTextInput(win);
        SDL_DestroyWindow(win);
        CORE.WindowHandle = nullptr;
    }

    // cleanup cursors
    for (int i = 0; i < RFX_CURSOR_COUNT; i++) {
        if (g_Cursors[i]) {
            SDL_DestroyCursor(g_Cursors[i]);
            g_Cursors[i] = nullptr;
        }
    }

    SDL_Quit();
}

void Backend_SetWindowFlags(RfxWindowFlags flags) {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (!win) {
        CORE.WindowFlags = flags;
        return;
    }

    RfxWindowFlags old = CORE.WindowFlags;
    if (flags == old)
        return;
    CORE.WindowFlags = flags;

    // borderless
    SDL_SetWindowBordered(win, (flags & RFX_WINDOW_BORDERLESS) ? false : true);

    // resize
    SDL_SetWindowResizable(win, (flags & RFX_WINDOW_NO_RESIZE) ? false : true);

    // floating
    SDL_SetWindowAlwaysOnTop(win, (flags & RFX_WINDOW_FLOATING) != 0);

    // visibility
    if (flags & RFX_WINDOW_HIDDEN)
        SDL_HideWindow(win);
    else
        SDL_ShowWindow(win);

    // maximized
    bool isMax = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
    bool wantMax = (flags & RFX_WINDOW_MAXIMIZED) != 0;
    if (wantMax && !isMax)
        SDL_MaximizeWindow(win);
    else if (!wantMax && isMax)
        SDL_RestoreWindow(win);

    // fullscreen
    bool fs = (flags & RFX_WINDOW_FULLSCREEN) != 0;
    bool oldFs = (old & RFX_WINDOW_FULLSCREEN) != 0;

    if (fs != oldFs) {
        if (fs) {
            // save state before going fullscreen
            int x, y, w, h;
            SDL_GetWindowPosition(win, &x, &y);
            SDL_GetWindowSize(win, &w, &h);
            CORE.SavedWindowPos[0] = x;
            CORE.SavedWindowPos[1] = y;
            CORE.SavedWindowSize[0] = w;
            CORE.SavedWindowSize[1] = h;

            SDL_SetWindowFullscreen(win, true);
        } else {
            SDL_SetWindowFullscreen(win, false);

            // restore state
            SDL_SetWindowSize(win, CORE.SavedWindowSize[0], CORE.SavedWindowSize[1]);
            SDL_SetWindowPosition(win, CORE.SavedWindowPos[0], CORE.SavedWindowPos[1]);
        }
    }

    bool vsync = (flags & RFX_WINDOW_VSYNC);
    if (vsync != CORE.VsyncEnable) {
        CORE.VsyncEnable = vsync;
        CORE.SwapChainWidth = 0; // trigger recreation
    }
}

bool Backend_WindowShouldClose() {
    return g_ShouldClose;
}

void Backend_PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: g_ShouldClose = true; break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            int mapped = MapSDLKey(event.key.key);
            if (mapped >= 0 && mapped < RFX_MAX_KEYS) {
                CORE.Input.keysCurrent[mapped] = (event.type == SDL_EVENT_KEY_DOWN);
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    Input_PushKeyPressed(mapped);
                }
            }
        } break;

        case SDL_EVENT_TEXT_INPUT: {
            const char* ptr = event.text.text;
            uint32_t codepoint;
            while ((codepoint = SDL_StepUTF8(&ptr, nullptr)) != 0) {
                if (codepoint != SDL_INVALID_UNICODE_CODEPOINT) {
                    Input_PushCharPressed(codepoint);
                }
            }
        } break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btn = -1;
            if (event.button.button == SDL_BUTTON_LEFT)
                btn = RFX_MOUSE_BUTTON_LEFT;
            else if (event.button.button == SDL_BUTTON_RIGHT)
                btn = RFX_MOUSE_BUTTON_RIGHT;
            else if (event.button.button == SDL_BUTTON_MIDDLE)
                btn = RFX_MOUSE_BUTTON_MIDDLE;

            if (btn >= 0 && btn < RFX_MAX_MOUSE_BUTTONS) {
                CORE.Input.mouseButtonsCurrent[btn] = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
        } break;

        case SDL_EVENT_MOUSE_MOTION: {
            CORE.Input.mouseX = event.motion.x;
            CORE.Input.mouseY = event.motion.y;
        } break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            CORE.FramebufferWidth = event.window.data1;
            CORE.FramebufferHeight = event.window.data2;
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED: CORE.IsFocused = true; break;

        case SDL_EVENT_WINDOW_FOCUS_LOST: CORE.IsFocused = false; break;

        case SDL_EVENT_WINDOW_MINIMIZED: CORE.IsMinimized = true; break;

        case SDL_EVENT_WINDOW_RESTORED: CORE.IsMinimized = false; break;
        }
    }
}

void Backend_GetWindowSize(int* width, int* height) {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (!win) {
        if (width)
            *width = 0;
        if (height)
            *height = 0;
        return;
    }

    SDL_GetWindowSize(win, width, height);
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

float Backend_GetWindowScale() {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (!win)
        return 1.0f;
    return SDL_GetWindowDisplayScale(win);
}

double Backend_GetTime() {
    uint64_t counter = SDL_GetPerformanceCounter();
    return (double)counter / (double)g_Frequency;
}

void Backend_SetMouseCursorVisible(bool visible) {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (win) {
        if (visible) {
            SDL_SetWindowRelativeMouseMode(win, false);
            SDL_ShowCursor();
        } else {
            SDL_SetWindowRelativeMouseMode(win, true);
            SDL_HideCursor();
            CORE.Input.firstMouseFrame = true;
        }
    }
}

void Backend_SetMouseCursor(RfxCursorType cursor) {
    if (cursor == RFX_CURSOR_DEFAULT) {
        if (g_Cursors[RFX_CURSOR_ARROW])
            SDL_SetCursor(g_Cursors[RFX_CURSOR_ARROW]);
        else
            SDL_SetCursor(SDL_GetDefaultCursor());
        return;
    }

    if (cursor < 0 || cursor >= RFX_CURSOR_COUNT)
        return;

    SDL_SystemCursor sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (cursor) {
    case RFX_CURSOR_ARROW: sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
    case RFX_CURSOR_IBEAM: sdlCursor = SDL_SYSTEM_CURSOR_TEXT; break;
    case RFX_CURSOR_CROSSHAIR: sdlCursor = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case RFX_CURSOR_HAND: sdlCursor = SDL_SYSTEM_CURSOR_POINTER; break;
    case RFX_CURSOR_RESIZE_EW: sdlCursor = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case RFX_CURSOR_RESIZE_NS: sdlCursor = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case RFX_CURSOR_RESIZE_NWSE: sdlCursor = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case RFX_CURSOR_RESIZE_NESW: sdlCursor = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case RFX_CURSOR_RESIZE_ALL: sdlCursor = SDL_SYSTEM_CURSOR_MOVE; break;
    case RFX_CURSOR_NOT_ALLOWED: sdlCursor = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    case RFX_CURSOR_RESIZE_NW: sdlCursor = SDL_SYSTEM_CURSOR_NW_RESIZE; break;
    case RFX_CURSOR_RESIZE_N: sdlCursor = SDL_SYSTEM_CURSOR_N_RESIZE; break;
    case RFX_CURSOR_RESIZE_NE: sdlCursor = SDL_SYSTEM_CURSOR_NE_RESIZE; break;
    case RFX_CURSOR_RESIZE_E: sdlCursor = SDL_SYSTEM_CURSOR_E_RESIZE; break;
    case RFX_CURSOR_RESIZE_SE: sdlCursor = SDL_SYSTEM_CURSOR_SE_RESIZE; break;
    case RFX_CURSOR_RESIZE_S: sdlCursor = SDL_SYSTEM_CURSOR_S_RESIZE; break;
    case RFX_CURSOR_RESIZE_SW: sdlCursor = SDL_SYSTEM_CURSOR_SW_RESIZE; break;
    case RFX_CURSOR_RESIZE_W: sdlCursor = SDL_SYSTEM_CURSOR_W_RESIZE; break;
    case RFX_CURSOR_WAIT: sdlCursor = SDL_SYSTEM_CURSOR_WAIT; break;
    case RFX_CURSOR_PROGRESS: sdlCursor = SDL_SYSTEM_CURSOR_PROGRESS; break;
    default: break;
    }

    if (!g_Cursors[cursor]) {
        g_Cursors[cursor] = SDL_CreateSystemCursor(sdlCursor);
    }

    if (g_Cursors[cursor]) {
        SDL_SetCursor(g_Cursors[cursor]);
    }
}

void Backend_GetNativeHandles(nri::Window& nriWindow) {
    SDL_Window* win = (SDL_Window*)CORE.WindowHandle;
    if (!win)
        return;

    SDL_PropertiesID props = SDL_GetWindowProperties(win);

    // clang-format off
#if (RAFX_PLATFORM == RAFX_WINDOWS)
    nriWindow.windows.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif (RAFX_PLATFORM == RAFX_WAYLAND)
    nriWindow.wayland.display = (wl_display*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    nriWindow.wayland.surface = (wl_surface*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
#elif (RAFX_PLATFORM == RAFX_X11)
    nriWindow.x11.dpy = (Display*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    nriWindow.x11.window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
#elif (RAFX_PLATFORM == RAFX_COCOA)
    nriWindow.metal.caMetalLayer = SDL_Metal_CreateView(win);
#endif
    // clang-format on
}

#endif // RAFX_BACKEND_SDL
