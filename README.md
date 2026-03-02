# vivid-cef

CEF (Chromium Embedded Framework) browser operator for [Vivid](https://github.com/seethroughlab/vivid).

Renders HTML/CSS/JavaScript/WebGL content as GPU textures. Use it for dashboards, data visualizations, WebGL effects, HTML overlays, and more.

## Installation

In Vivid's package browser, search for `vivid-cef` and click Install. Or from the CLI:

```
vivid packages install https://github.com/seethroughlab/vivid-cef
```

**Note:** First install downloads ~100MB of CEF binaries and compiles the wrapper library, which takes a few minutes.

## Browser Operator

| Parameter   | Type  | Default | Range      | Description                            |
|-------------|-------|---------|------------|----------------------------------------|
| url         | File  |         |            | URL or local HTML file path            |
| zoom        | Float | 1.0     | 0.25 – 4.0 | Browser zoom level                    |
| transparent | Bool  | false   |            | Transparent background (for overlays)  |
| frame_rate  | Int   | 60      | 1 – 120    | CEF rendering frame rate               |

### Output

- **texture** (GPU_TEXTURE) — rendered web content as a GPU texture

## Examples

- `examples/hello-world.html` — basic HTML/CSS rendering
- `examples/webgl-cube.html` — rotating WebGL cube
- `examples/dashboard.html` — animated data dashboard
- `examples/transparent-overlay.html` — transparent overlay with live clock
- `examples/interactive.html` — mouse/keyboard interaction test (cursor tracking, click dots, scroll resize, key display)

## How It Works

1. CEF renders web content offscreen to a CPU pixel buffer (OnPaint callback)
2. Pixels are uploaded to a WebGPU staging texture via `wgpuQueueWriteTexture`
3. A fullscreen blit pass copies the staging texture to the operator's output

CEF lifecycle is reference-counted: the first Browser operator initializes CEF, subsequent operators share the same CEF process, and CEF shuts down when the last Browser operator is destroyed.

### Input Forwarding

When the Vivid UI is hidden (tilde key), mouse and keyboard events are forwarded to Browser operators via the `VividInputState` API. Events are translated from GLFW to CEF format:

- Mouse move, click, scroll
- Key press/release with modifier support
- Character input (for text fields)

## Requirements

- CMake 3.16+
- macOS (arm64/x86_64), Windows, or Linux
- Internet connection for first build (downloads CEF from Spotify CDN)

## Known Issues

### Fixed: CEF re-initialization crash

CEF cannot be initialized, shut down, and re-initialized in the same process. Vivid's plugin probing (`scan_deferred`) opens each dylib, calls `vivid_descriptor()`, then `dlclose`s it. When the dylib was later reopened for real use, `CefInitialize()` would crash with SIGSEGV.

**Fix:** `CefManager::acquire()` was moved out of the `BrowserOp` constructor into `process()`, so CEF is only initialized when the operator actually runs — not during descriptor probing.

### Fixed: CEF GPU subprocess crash loop (libGLESv2.dylib not found)

CEF's GPU subprocess (`vivid-cef-helper --type=gpu-process`) looks for `libGLESv2.dylib` and `libEGL.dylib` next to the helper binary. These libraries ship inside `Chromium Embedded Framework.framework/Libraries/` but the subprocess doesn't look there.

**Fix (temporary):** Symlinks from the build directory to the framework Libraries. Needs to be made permanent via a post-build step in CMakeLists.txt.

### WIP: Browser not rendering (black output)

CEF initializes, the browser is created, and no errors are reported, but `OnPaint` is never called. The `url.str_value` may not be reaching `update_url()` — needs debug logging to verify the file parameter pipeline is working end-to-end. The URL loading path (graph `string_params` -> scheduler `file_param_storage` -> `ctx->file_param_values` -> `vivid_process` wrapper -> `url.str_value`) has several handoff points to check.

## Other Limitations

- **CPU pixel path** — CEF's OnAcceleratedPaint with shared textures is not available on macOS. Pixel upload is ~0.5-1ms for 720p on Apple Silicon.
