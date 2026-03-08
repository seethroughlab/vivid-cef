# AGENTS.md — vivid-cef

## Project overview

CEF (Chromium Embedded Framework) browser operator plugin for [Vivid](https://vivid.engineering). Renders HTML/CSS/JS offscreen and outputs the result as a GPU texture. Runs in single-process mode due to Mach port IPC limitations when loaded as a plugin via `dlopen()`.

## Build

CMake-based. First build downloads CEF (~100MB) and takes a few minutes.

```bash
cmake -B build -S . \
  -DVIVID_SRC_DIR=/path/to/vivid/source \
  -DVIVID_BUILD_DIR=/path/to/vivid/build
cmake --build build
```

**Required variables:**
- `VIVID_SRC_DIR` — Vivid source tree (contains `src/`)
- `VIVID_BUILD_DIR` — Vivid build tree (contains `_deps/wgpu*-src/include`)

**Outputs:** `browser.dylib` (plugin), `vivid-cef-helper` (CEF subprocess), plus the CEF framework.

C++17. No automated tests — test manually by loading example graphs in Vivid.

## File layout

```
src/
  browser_op.h/cpp      BrowserOp — GPU operator (main entry point)
  cef_manager.h/cpp     CefManager — singleton CEF lifecycle (+ VividCefApp)
  cef_client.h/cpp      VividCefClient + VividRenderHandler
subprocess/
  main.cpp              vivid-cef-helper executable
examples/               HTML test pages (hello.html, dashboard.html, etc.)
graphs/                 Vivid graph files for manual testing
tests/                  Placeholder (empty)
```

## Architecture

### Key classes

- **BrowserOp** — Vivid GPU operator. Creates a CEF browser, receives pixels, uploads to GPU, blits to output texture. Registered via `VIVID_REGISTER(BrowserOp)`.
- **CefManager** — Static singleton with reference counting. First `acquire()` calls `CefInitialize()`; last `release()` calls `CefShutdown()`. Pumps CEF message loop once per frame via `pump_once(frame)`.
- **VividRenderHandler** — Receives BGRA pixel buffers from CEF's `OnPaint()` callback. Thread-safe via mutex.
- **VividCefClient** — Routes CEF handler interfaces. Supports deferred URL loading (URL set before browser exists → loaded in `OnAfterCreated()`).
- **VividCefApp** — Configures CEF command-line switches (single-process, disable-gpu, etc.).

### Data flow

1. CEF renders HTML offscreen → calls `VividRenderHandler::OnPaint()` with BGRA pixels
2. `BrowserOp::process()` locks mutex, copies pixels to a WebGPU staging texture via `wgpuQueueWriteTexture()`
3. Fullscreen blit pass samples staging texture → writes to output GPU texture

### CEF lifecycle

1. Vivid probes the plugin (calls `vivid_descriptor()` then `dlclose()`) — CEF must NOT init here
2. On first `BrowserOp::process()` call, `CefManager::acquire()` initializes CEF (deferred init)
3. Multiple BrowserOp instances share one CEF via refcount
4. Last destructor triggers `CefShutdown()`

## Key design decisions

**Single-process + in-process-gpu mode** — CEF 120 subprocesses (GPU, renderer) fail on macOS 26 before main() runs due to binary compatibility issues. `--single-process` keeps the renderer in-process; `--in-process-gpu` keeps the GPU service in-process. `--use-angle=swiftshader` provides software WebGL 2 without a GPU subprocess. `--disable-gpu-compositing` routes pixels through `OnPaint` (CPU path).

**CPU pixel path** — CEF's `OnAcceleratedPaint` with shared textures isn't available on macOS. `OnPaint()` delivers BGRA pixels; we upload them to GPU each frame. ~0.5–1ms at 720p on Apple Silicon.

**Deferred init** — `CefInitialize()` crashes if called during Vivid's descriptor probe (which `dlclose()`s the plugin immediately). Init is deferred to the first `process()` call.

**Reference-counted lifecycle** — `CefManager` uses `std::atomic<int>` refcount + mutex-guarded init. Multiple operators share one CEF instance.

## Coding conventions

- **Classes:** PascalCase (`BrowserOp`, `CefManager`)
- **Member variables:** snake_case with trailing underscore (`staging_tex_`, `last_url_`)
- **Static members:** `s_` prefix (`s_refcount`, `s_initialized`)
- **File-scope statics:** `g_` prefix (`g_init_mutex`)
- **Constants:** `k` prefix (`kBrowserWidth`, `kBlitFragment`)
- **Free functions:** snake_case (`glfw_to_cef_button`)
- **Log messages:** `[vivid-cef]` prefix to stderr

## Important pitfalls

**WebGL via SwiftShader** — Hardware GPU is unavailable (CEF subprocess binary incompatibility on macOS 26). WebGL 2 works via SwiftShader (software rasterizer) using `--use-angle=swiftshader`. WebGPU in the browser is not available. Performance is software-level but functional.

**CEF probe crash** — If `CefInitialize()` runs during Vivid's plugin probe, the subsequent `dlclose()` corrupts CEF state. The fix is deferred init (already implemented). Don't move `acquire()` back to the constructor.

**bytesPerRow alignment** — WebGPU requires texture row stride to be a multiple of 256 bytes. CEF delivers `width * 4`. `upload_pixels()` pads rows when needed: `aligned_bpr = (src_bpr + 255) & ~255u`.

**CEF cannot re-initialize** — Once `CefShutdown()` runs, `CefInitialize()` will crash in the same process. The refcount design prevents premature shutdown.

**Keychain prompts (macOS)** — CEF triggers macOS keychain access. Mitigated with `--use-mock-keychain` and temp cache dir (`$TMPDIR/vivid-cef-cache`), but some prompts may still appear.

**CEF download size** — First build downloads ~100MB, extracts to ~500MB. The `_deps/cef/` directory is cached; delete it to force re-download.
