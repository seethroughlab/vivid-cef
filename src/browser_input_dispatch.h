#pragma once

#include "cef_client.h"
#include "operator_api/input_state.h"

void forward_browser_input_events(CefRefPtr<VividCefClient> client,
                                  const VividInputState* input,
                                  int width,
                                  int height);

