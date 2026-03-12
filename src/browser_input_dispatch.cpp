#include "browser_input_dispatch.h"

#include <include/cef_browser.h>

namespace {

cef_mouse_button_type_t glfw_to_cef_button(int button) {
    switch (button) {
        case 0: return MBT_LEFT;
        case 1: return MBT_RIGHT;
        case 2: return MBT_MIDDLE;
        default: return MBT_LEFT;
    }
}

uint32_t glfw_to_cef_modifiers(int mods, int buttons_held = 0) {
    uint32_t cef_mods = 0;
    if (mods & 1) cef_mods |= EVENTFLAG_SHIFT_DOWN;
    if (mods & 2) cef_mods |= EVENTFLAG_CONTROL_DOWN;
    if (mods & 4) cef_mods |= EVENTFLAG_ALT_DOWN;
    if (mods & 8) cef_mods |= EVENTFLAG_COMMAND_DOWN;
    if (buttons_held & 1) cef_mods |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    if (buttons_held & 2) cef_mods |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    if (buttons_held & 4) cef_mods |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    return cef_mods;
}

int glfw_to_cef_keycode(int glfw_key) {
    if (glfw_key >= 32 && glfw_key <= 126) return glfw_key;
    switch (glfw_key) {
        case 256: return 0x1B;
        case 257: return 0x0D;
        case 258: return 0x09;
        case 259: return 0x08;
        case 260: return 0x2D;
        case 261: return 0x2E;
        case 262: return 0x27;
        case 263: return 0x25;
        case 264: return 0x28;
        case 265: return 0x26;
        case 266: return 0x21;
        case 267: return 0x22;
        case 268: return 0x24;
        case 269: return 0x23;
        case 280: return 0x14;
        case 290: return 0x70;
        case 291: return 0x71;
        case 292: return 0x72;
        case 293: return 0x73;
        case 294: return 0x74;
        case 295: return 0x75;
        case 296: return 0x76;
        case 297: return 0x77;
        case 298: return 0x78;
        case 299: return 0x79;
        case 300: return 0x7A;
        case 301: return 0x7B;
        case 340: return 0x10;
        case 341: return 0x11;
        case 342: return 0x12;
        case 343: return 0x5B;
        case 344: return 0x10;
        case 345: return 0x11;
        case 346: return 0x12;
        case 347: return 0x5C;
        case 32:  return 0x20;
        default:  return 0;
    }
}

int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void forward_browser_input_events(CefRefPtr<VividCefClient> client,
                                  const VividInputState* input,
                                  int width,
                                  int height) {
    if (!input || !client || !client->browser()) return;
    auto host = client->browser()->GetHost();
    for (uint32_t i = 0; i < input->event_count; ++i) {
        const auto& ev = input->events[i];
        int px = static_cast<int>(ev.mouse_x * width);
        int py = static_cast<int>(ev.mouse_y * height);

        switch (ev.type) {
            case VIVID_INPUT_MOUSE_MOVE: {
                CefMouseEvent me;
                me.x = px;
                me.y = py;
                me.modifiers = glfw_to_cef_modifiers(ev.modifiers, input->buttons_held);
                host->SendMouseMoveEvent(me, false);
                break;
            }
            case VIVID_INPUT_MOUSE_BUTTON: {
                CefMouseEvent me;
                me.x = px;
                me.y = py;
                me.modifiers = glfw_to_cef_modifiers(ev.modifiers, input->buttons_held);
                bool up = (ev.action == 0);
                host->SendMouseClickEvent(me, glfw_to_cef_button(ev.button), up, 1);
                break;
            }
            case VIVID_INPUT_MOUSE_SCROLL: {
                CefMouseEvent me;
                me.x = px;
                me.y = py;
                me.modifiers = glfw_to_cef_modifiers(ev.modifiers, input->buttons_held);
                host->SendMouseWheelEvent(me,
                                          static_cast<int>(ev.scroll_dx * 120),
                                          static_cast<int>(ev.scroll_dy * 120));
                break;
            }
            case VIVID_INPUT_KEY: {
                CefKeyEvent ke;
                ke.windows_key_code = glfw_to_cef_keycode(ev.key);
                ke.native_key_code = ev.scancode;
                ke.modifiers = glfw_to_cef_modifiers(ev.modifiers);
                ke.is_system_key = false;
                if (ev.action == 1 || ev.action == 2)
                    ke.type = KEYEVENT_RAWKEYDOWN;
                else
                    ke.type = KEYEVENT_KEYUP;
                host->SendKeyEvent(ke);
                break;
            }
            case VIVID_INPUT_CHAR: {
                CefKeyEvent ke;
                ke.type = KEYEVENT_CHAR;
                ke.windows_key_code = static_cast<int>(ev.codepoint);
                ke.character = static_cast<char16_t>(ev.codepoint);
                ke.unmodified_character = ke.character;
                ke.native_key_code = 0;
                ke.modifiers = glfw_to_cef_modifiers(ev.modifiers);
                ke.is_system_key = false;
                host->SendKeyEvent(ke);
                break;
            }
        }
    }
}

void forward_browser_input_events_viewport(
    CefRefPtr<VividCefClient> client,
    const VividInputState* input,
    int width, int height,
    const BrowserInputViewport& vp,
    bool& mouse_was_inside) {
    if (!input || !client || !client->browser()) return;
    if (vp.w == 0.0f || vp.h == 0.0f) return;

    auto host = client->browser()->GetHost();

    for (uint32_t i = 0; i < input->event_count; ++i) {
        const auto& ev = input->events[i];

        bool is_mouse = (ev.type == VIVID_INPUT_MOUSE_MOVE ||
                         ev.type == VIVID_INPUT_MOUSE_BUTTON ||
                         ev.type == VIVID_INPUT_MOUSE_SCROLL);

        if (is_mouse) {
            bool inside = (ev.mouse_x >= vp.x && ev.mouse_x < vp.x + vp.w &&
                           ev.mouse_y >= vp.y && ev.mouse_y < vp.y + vp.h);

            float local_x = (ev.mouse_x - vp.x) / vp.w;
            float local_y = (ev.mouse_y - vp.y) / vp.h;
            int px = clamp_int(static_cast<int>(local_x * width), 0, width - 1);
            int py = clamp_int(static_cast<int>(local_y * height), 0, height - 1);

            if (inside) {
                mouse_was_inside = true;

                CefMouseEvent me;
                me.x = px;
                me.y = py;
                me.modifiers = glfw_to_cef_modifiers(ev.modifiers, input->buttons_held);

                switch (ev.type) {
                    case VIVID_INPUT_MOUSE_MOVE:
                        host->SendMouseMoveEvent(me, false);
                        break;
                    case VIVID_INPUT_MOUSE_BUTTON: {
                        bool up = (ev.action == 0);
                        host->SendMouseClickEvent(me, glfw_to_cef_button(ev.button), up, 1);
                        break;
                    }
                    case VIVID_INPUT_MOUSE_SCROLL:
                        host->SendMouseWheelEvent(me,
                                                  static_cast<int>(ev.scroll_dx * 120),
                                                  static_cast<int>(ev.scroll_dy * 120));
                        break;
                    default: break;
                }
            } else {
                // Outside viewport
                CefMouseEvent me;
                me.x = px;
                me.y = py;
                me.modifiers = glfw_to_cef_modifiers(ev.modifiers, input->buttons_held);

                if (ev.type == VIVID_INPUT_MOUSE_MOVE && mouse_was_inside) {
                    host->SendMouseMoveEvent(me, true);  // mouseLeave
                    mouse_was_inside = false;
                } else if (ev.type == VIVID_INPUT_MOUSE_BUTTON && ev.action == 0) {
                    // Forward button release so CEF doesn't get stuck in drag state
                    host->SendMouseClickEvent(me, glfw_to_cef_button(ev.button), true, 1);
                }
                // Skip press/scroll outside viewport
            }
        } else {
            // Keyboard events — only forward if focused
            if (!vp.keyboard_focus) continue;

            switch (ev.type) {
                case VIVID_INPUT_KEY: {
                    CefKeyEvent ke;
                    ke.windows_key_code = glfw_to_cef_keycode(ev.key);
                    ke.native_key_code = ev.scancode;
                    ke.modifiers = glfw_to_cef_modifiers(ev.modifiers);
                    ke.is_system_key = false;
                    if (ev.action == 1 || ev.action == 2)
                        ke.type = KEYEVENT_RAWKEYDOWN;
                    else
                        ke.type = KEYEVENT_KEYUP;
                    host->SendKeyEvent(ke);
                    break;
                }
                case VIVID_INPUT_CHAR: {
                    CefKeyEvent ke;
                    ke.type = KEYEVENT_CHAR;
                    ke.windows_key_code = static_cast<int>(ev.codepoint);
                    ke.character = static_cast<char16_t>(ev.codepoint);
                    ke.unmodified_character = ke.character;
                    ke.native_key_code = 0;
                    ke.modifiers = glfw_to_cef_modifiers(ev.modifiers);
                    ke.is_system_key = false;
                    host->SendKeyEvent(ke);
                    break;
                }
                default: break;
            }
        }
    }
}
