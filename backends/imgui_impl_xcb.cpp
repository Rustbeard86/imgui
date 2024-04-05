// dear imgui: Platform Binding for Linux (standard X11 API for 32 and 64 bits applications)
// This needs to be used along with a Renderer (e.g. OpenGL3, Vulkan..)

// https://www.uninformativ.de/blog/postings/2017-04-02/0/POSTING-en.html
// https://stackoverflow.com/questions/27378318/c-get-string-from-clipboard-on-linux

// Implemented features:
//  [X] Platform: Clipboard support
//  [ ] Platform: Mouse cursor shape and visibility. Disable with XK_io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Keyboard arrays indexed using
//  [ ] Platform: Gamepad support. Enabled with XK_io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.

#include "imgui.h"
#include "imgui_impl_xcb.h"

#include <X11/keysym.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
//#include <xcb/xfixes.h>
//#include <xcb/xcb_cursor.h>
//#include <xcb/randr.h>

#define explicit c_explicit
#include <xcb/xkb.h>
#undef explicit

#include <cstdlib>
#include <climits>
#include <ctime>
#include <cstdint>

//#include <iostream>

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2024-04-04: Initial xcb implementation.

struct ImGui_ImplXcb_Data
{
    xcb_connection_t*    hConnection;
    xcb_key_symbols_t*   KeySyms;
    bool                 MouseTracked;
    int                  MouseButtonsDown;
    uint64_t             Time;
    uint64_t             TicksPerSecond;
    ImGuiMouseCursor     LastMouseCursor;
    bool                 HasGamepad;
    bool                 WantUpdateHasGamepad;

    ImGui_ImplXcb_Data()      { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static ImGui_ImplXcb_Data* ImGui_ImplXcb_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplXcb_Data*)ImGui::GetIO().BackendPlatformUserData : NULL;
}

IMGUI_IMPL_API bool     ImGui_ImplXcb_Init(void* connection)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");

    // Setup backend capabilities flags
    ImGui_ImplXcb_Data* bd = IM_NEW(ImGui_ImplXcb_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "imgui_impl_xcb";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    //io.GetClipboardTextFn = ImGui_ImplX11_GetClipboardText;
    //io.SetClipboardTextFn = ImGui_ImplX11_SetClipboardText;

    timespec ts, tsres;
    clock_getres(CLOCK_MONOTONIC_RAW, &tsres);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    bd->hConnection = reinterpret_cast<xcb_connection_t*>(connection);
    bd->WantUpdateHasGamepad = true;
    bd->TicksPerSecond = 1000000000.0f / (static_cast<uint64_t>(tsres.tv_nsec) + static_cast<uint64_t>(tsres.tv_sec) * 1000000000);
    bd->Time = static_cast<uint64_t>(ts.tv_nsec) + static_cast<uint64_t>(ts.tv_sec) * 1000000000;
    bd->LastMouseCursor = ImGuiMouseCursor_COUNT;
    //bd->ClipboardBuffer = (char*)malloc(sizeof(char) * 256);
    //bd->ClipboardBufferSize = 256;
    //
    //bd->XQueryPointer = XQueryPointerFunction == nullptr
    //    ? &XQueryPointer
    //    : (decltype(XQueryPointer)*)XQueryPointerFunction;

    bd->KeySyms = xcb_key_symbols_alloc(bd->hConnection);

    //bd->BufId = XInternAtom(bd->hDisplay, "CLIPBOARD", False);
    //bd->PropId = XInternAtom(bd->hDisplay, "XSEL_DATA", False);
    //bd->FmtIdUtf8String = XInternAtom(bd->hDisplay, "UTF8_STRING", False);
    //bd->IncrId = XInternAtom(bd->hDisplay, "INCR", False);

    return true;
}

IMGUI_IMPL_API void     ImGui_ImplXcb_Shutdown()
{
    ImGui_ImplXcb_Data* bd = ImGui_ImplXcb_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    io.GetClipboardTextFn = nullptr;
    io.SetClipboardTextFn = nullptr;
    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;

    xcb_key_symbols_free(bd->KeySyms);
    //free(bd->ClipboardBuffer);
    IM_DELETE(bd);
}

static void ImGui_ImplXcb_AddKeyEvent(ImGuiKey key, bool down, int native_keycode, int native_scancode = -1)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, native_scancode); // To support legacy indexing (<1.87 user code)
    IM_UNUSED(native_scancode);
}

static void ImGui_ImplXcb_UpdateKeyModifiers()
{
    //ImGui_ImplXcb_Data* bd = ImGui_ImplXcb_GetBackendData();
    //ImGuiIO& io = ImGui::GetIO();
    //
    //bool k;
    //char szKey[32];
    //XQueryKeymap(bd->hDisplay, szKey);
    //
    //io.AddKeyEvent(ImGuiMod_Ctrl, GetKeyState(bd->hDisplay, XK_Control_L, szKey) || GetKeyState(bd->hDisplay, XK_Control_R, szKey));
    //io.AddKeyEvent(ImGuiMod_Shift, GetKeyState(bd->hDisplay, XK_Shift_L, szKey) || GetKeyState(bd->hDisplay, XK_Shift_R, szKey));
    //io.AddKeyEvent(ImGuiMod_Alt, GetKeyState(bd->hDisplay, XK_Alt_L, szKey) || GetKeyState(bd->hDisplay, XK_Alt_R, szKey));
    //io.AddKeyEvent(ImGuiMod_Super, GetKeyState(bd->hDisplay, XK_Super_L, szKey) || GetKeyState(bd->hDisplay, XK_Super_R, szKey));
}

IMGUI_IMPL_API bool     ImGui_ImplXcb_NewFrame()
{
    return false;
}

static ImGuiKey ImGui_ImplXcb_VirtualKeyToImGuiKey(uint32_t param)
{
    switch (param)
    {
        case XK_Tab         :  return ImGuiKey_Tab;
        case XK_ISO_Left_Tab:  return ImGuiKey_Tab;
        case XK_Left        : case XK_KP_Left     : return ImGuiKey_LeftArrow;
        case XK_Right       : case XK_KP_Right    : return ImGuiKey_RightArrow;
        case XK_Up          : case XK_KP_Up       : return ImGuiKey_UpArrow;
        case XK_Down        : case XK_KP_Down     : return ImGuiKey_DownArrow;
        case XK_Prior       : case XK_KP_Page_Up  : return ImGuiKey_PageUp;
        case XK_Next        : case XK_KP_Page_Down: return ImGuiKey_PageDown;
        case XK_Home        : case XK_KP_Home     : case XK_KP_Begin : return ImGuiKey_Home;
        case XK_End         : case XK_KP_End      : return ImGuiKey_End;
        case XK_Insert      : case XK_KP_Insert   : return ImGuiKey_Insert;
        case XK_Delete      : return ImGuiKey_Delete;
        case XK_BackSpace   : return ImGuiKey_Backspace;
        case XK_space       : return ImGuiKey_Space;
        case XK_Return      : return ImGuiKey_Enter;
        case XK_Escape      : return ImGuiKey_Escape;
        case XK_apostrophe  : return ImGuiKey_Apostrophe;
        case XK_comma: return ImGuiKey_Comma;
        case XK_minus: return ImGuiKey_Minus;
        case XK_period: return ImGuiKey_Period;
        case XK_slash: return ImGuiKey_Slash;
        case XK_semicolon: return ImGuiKey_Semicolon;
        case XK_equal: return ImGuiKey_Equal;
        case XK_bracketleft: return ImGuiKey_LeftBracket;
        case XK_backslash: return ImGuiKey_Backslash;
        case XK_bracketright: return ImGuiKey_RightBracket;
        case XK_grave: return ImGuiKey_GraveAccent;
        case XK_Caps_Lock: return ImGuiKey_CapsLock;
        case XK_Scroll_Lock: return ImGuiKey_ScrollLock;
        case XK_Num_Lock: return ImGuiKey_NumLock;
        case XK_Print: return ImGuiKey_PrintScreen;
        case XK_Pause: return ImGuiKey_Pause;
        case XK_KP_0: return ImGuiKey_Keypad0;
        case XK_KP_1: return ImGuiKey_Keypad1;
        case XK_KP_2: return ImGuiKey_Keypad2;
        case XK_KP_3: return ImGuiKey_Keypad3;
        case XK_KP_4: return ImGuiKey_Keypad4;
        case XK_KP_5: return ImGuiKey_Keypad5;
        case XK_KP_6: return ImGuiKey_Keypad6;
        case XK_KP_7: return ImGuiKey_Keypad7;
        case XK_KP_8: return ImGuiKey_Keypad8;
        case XK_KP_9: return ImGuiKey_Keypad9;
        case XK_KP_Decimal : return ImGuiKey_KeypadDecimal;
        case XK_KP_Divide  : return ImGuiKey_KeypadDivide;
        case XK_KP_Multiply: return ImGuiKey_KeypadMultiply;
        case XK_KP_Subtract: return ImGuiKey_KeypadSubtract;
        case XK_KP_Add     : return ImGuiKey_KeypadAdd;
        case XK_KP_Enter   : return ImGuiKey_KeypadEnter;
        case XK_Shift_L    : return ImGuiKey_LeftShift;
        case XK_Control_L  : return ImGuiKey_LeftCtrl;
        case XK_Alt_L      : return ImGuiKey_LeftAlt;
        case XK_Super_L    : return ImGuiKey_LeftSuper;
        case XK_Shift_R    : return ImGuiKey_RightShift;
        case XK_Control_R  : return ImGuiKey_RightCtrl;
        case XK_Alt_R      : return ImGuiKey_RightAlt;
        case XK_Super_R    : return ImGuiKey_RightSuper;
        //case XK_APPS: return ImGuiKey_Menu;
        case XK_0 : return ImGuiKey_0;
        case XK_1 : return ImGuiKey_1;
        case XK_2 : return ImGuiKey_2;
        case XK_3 : return ImGuiKey_3;
        case XK_4 : return ImGuiKey_4;
        case XK_5 : return ImGuiKey_5;
        case XK_6 : return ImGuiKey_6;
        case XK_7 : return ImGuiKey_7;
        case XK_8 : return ImGuiKey_8;
        case XK_9 : return ImGuiKey_9;
        case XK_a : case XK_A : return ImGuiKey_A;
        case XK_b : case XK_B : return ImGuiKey_B;
        case XK_c : case XK_C : return ImGuiKey_C;
        case XK_d : case XK_D : return ImGuiKey_D;
        case XK_e : case XK_E : return ImGuiKey_E;
        case XK_f : case XK_F : return ImGuiKey_F;
        case XK_g : case XK_G : return ImGuiKey_G;
        case XK_h : case XK_H : return ImGuiKey_H;
        case XK_i : case XK_I : return ImGuiKey_I;
        case XK_j : case XK_J : return ImGuiKey_J;
        case XK_k : case XK_K : return ImGuiKey_K;
        case XK_l : case XK_L : return ImGuiKey_L;
        case XK_m : case XK_M : return ImGuiKey_M;
        case XK_n : case XK_N : return ImGuiKey_N;
        case XK_o : case XK_O : return ImGuiKey_O;
        case XK_p : case XK_P : return ImGuiKey_P;
        case XK_q : case XK_Q : return ImGuiKey_Q;
        case XK_r : case XK_R : return ImGuiKey_R;
        case XK_s : case XK_S : return ImGuiKey_S;
        case XK_t : case XK_T : return ImGuiKey_T;
        case XK_u : case XK_U : return ImGuiKey_U;
        case XK_v : case XK_V : return ImGuiKey_V;
        case XK_w : case XK_W : return ImGuiKey_W;
        case XK_x : case XK_X : return ImGuiKey_X;
        case XK_y : case XK_Y : return ImGuiKey_Y;
        case XK_z : case XK_Z : return ImGuiKey_Z;
        case XK_F1: return ImGuiKey_F1;
        case XK_F2: return ImGuiKey_F2;
        case XK_F3: return ImGuiKey_F3;
        case XK_F4: return ImGuiKey_F4;
        case XK_F5: return ImGuiKey_F5;
        case XK_F6: return ImGuiKey_F6;
        case XK_F7: return ImGuiKey_F7;
        case XK_F8: return ImGuiKey_F8;
        case XK_F9: return ImGuiKey_F9;
        case XK_F10: return ImGuiKey_F10;
        case XK_F11: return ImGuiKey_F11;
        case XK_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

static int ImGui_ImplXcb_GetKeySymFromKeyCodeAndModifiers(xcb_key_press_event_t* event, int keycode, int modifiers, ImGui_ImplXcb_Data* bd)
{
    uint32_t col = modifiers & (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_LOCK) ? XCB_MOD_MASK_SHIFT : 0;
    int vk = xcb_key_press_lookup_keysym(bd->KeySyms, event, col);

    if ((modifiers & XCB_MOD_MASK_2) && vk >= XK_KP_Space && vk <= XK_KP_9) // KeyPad keys
    {
        vk = xcb_key_press_lookup_keysym(bd->KeySyms, event, XCB_MOD_MASK_SHIFT);
    }

    return vk;
}

static int ImGui_ImplXcb_GetVirtualKeyChar(int vk)
{
    if (vk < 256)
        return vk;

    switch (vk)
    {
        case XK_KP_0: return '0';
        case XK_KP_1: return '1';
        case XK_KP_2: return '2';
        case XK_KP_3: return '3';
        case XK_KP_4: return '4';
        case XK_KP_5: return '5';
        case XK_KP_6: return '6';
        case XK_KP_7: return '7';
        case XK_KP_8: return '8';
        case XK_KP_9: return '9';
    }

    return 0;
}

static int ImGui_ImplXcb_HandleKeyEvent(xcb_key_press_event_t* event, ImGui_ImplXcb_Data* bd, ImGuiIO& io)
{
    const bool is_key_down = (event->response_type & ~0x80) == XCB_KEY_PRESS;
    int vk = ImGui_ImplXcb_GetKeySymFromKeyCodeAndModifiers(event, event->detail, event->state, bd);
    if (vk == XCB_NO_SYMBOL)
        return vk;

    if (vk >= 0x1000100 && vk <= 0x110ffff)
    {
        if (is_key_down)
            io.AddInputCharacterUTF16(vk);
    }
    else
    {
        // Submit modifiers
        ImGui_ImplXcb_UpdateKeyModifiers();
        
        const ImGuiKey key = ImGui_ImplXcb_VirtualKeyToImGuiKey(vk);
        if (key != ImGuiKey_None)
            ImGui_ImplXcb_AddKeyEvent(key, is_key_down, vk, event->detail);
        
        if (is_key_down)
        {
            int keyChar = ImGui_ImplXcb_GetVirtualKeyChar(vk);
            if (keyChar != XCB_NO_SYMBOL)
                io.AddInputCharacter(keyChar);
        }
    }

    return vk;
}

IMGUI_IMPL_API int ImGui_ImplXcb_EventHandler(xcb_generic_event_t* event, xcb_generic_event_t* next_event)
{
    ImGui_ImplXcb_Data* bd = ImGui_ImplXcb_GetBackendData();
    if (ImGui::GetCurrentContext() == NULL)
        return 0;


    ImGuiIO& io = ImGui::GetIO();
    const auto eventType = event->response_type & ~0x80;
    switch (eventType)
    {
        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE:
        {
            xcb_button_press_event_t* e = (xcb_button_press_event_t*)event;
            const bool is_key_down = eventType == XCB_BUTTON_PRESS;
            // TODO: Call io.AddMousePosEvent before calling AddMouseButtonEvent ?
            switch (e->detail)
            {
            case 1:
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, is_key_down);
                break;

            case 2:
                io.AddMouseButtonEvent(ImGuiMouseButton_Middle, is_key_down);
                break;

            case 3:
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, is_key_down);
                break;

            case 4: // Mouse wheel up
                if (is_key_down)
                    io.AddMouseWheelEvent(0, 1);
                break;

            case 5: // Mouse wheel down
                if (is_key_down)
                    io.AddMouseWheelEvent(0, -1);
                break;
            }
        }
        return 0;

        case XCB_KEY_PRESS:
        case XCB_KEY_RELEASE:
        {
            xcb_key_press_event_t* e = (xcb_key_press_event_t*)event;
            xcb_key_press_event_t* ne = (xcb_key_press_event_t*)next_event;
            if (ne != nullptr)
            {
                // We should check the keycode too, but there are complex behaviors when holding Shift and pressing a Keypad key.
                // SHIFT + KP 1
                // Shift KeyPress  : Serial 1 <-- Pressed  Shift
                // Shift KeyRelease: Serial 2 <-- Pressed  KP 1
                // KP1   KeyPress  : Serial 2
                // KP1   KeyRelease: Serial 3 <-- Released KP 1
                // Shift KeyPress  : Serial 3
                // Shift KeyRelease: Serial 4 <-- Released Shift
                if (ne->response_type != XCB_KEY_PRESS || ne->sequence != e->sequence/*|| ne->xkey.keycode != e.xkey.keycode*/)
                {
                    ImGui_ImplXcb_HandleKeyEvent(e, bd, io);
                    ImGui_ImplXcb_HandleKeyEvent(ne, bd, io);
                }
            }
            else
            {
                ImGui_ImplXcb_HandleKeyEvent(e, bd, io);
            }
        }
        return 0;

        case XCB_FOCUS_OUT:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

        case XCB_FOCUS_IN:
            // False because we don't always receive XCB_MOTION_NOTIFY
            bd->MouseTracked = false;
            io.SetAppAcceptingEvents(eventType == XCB_FOCUS_IN);
            io.AddFocusEvent(eventType == XCB_FOCUS_IN);
            return 0;
    }
    return false;
}
