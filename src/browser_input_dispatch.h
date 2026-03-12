#pragma once

#include "cef_client.h"
#include "operator_api/input_state.h"

void forward_browser_input_events(CefRefPtr<VividCefClient> client,
                                  const VividInputState* input,
                                  int width,
                                  int height);

struct BrowserInputViewport {
    float x, y, w, h;    // normalized [0,1] screen rect where texture is displayed
    bool  keyboard_focus; // whether this browser receives keyboard events
};

void forward_browser_input_events_viewport(
    CefRefPtr<VividCefClient> client,
    const VividInputState* input,
    int width, int height,
    const BrowserInputViewport& viewport,
    bool& mouse_was_inside);

