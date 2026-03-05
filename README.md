# vivid-cef

CEF (Chromium Embedded Framework) browser operator for [Vivid](https://github.com/seethroughlab/vivid).

Renders HTML/CSS/JavaScript/WebGL content as GPU textures. Use it for dashboards, data visualizations, WebGL effects, HTML overlays, and more.

## Preview

![vivid-cef preview](docs/images/preview.png)


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

HTML pages:

- `examples/hello-world.html` — basic HTML/CSS rendering
- `examples/hello.html` — minimal hello page
- `examples/webgl-cube.html` — rotating WebGL cube (won't render — see Known Issues)
- `examples/dashboard.html` — animated data dashboard
- `examples/transparent-overlay.html` — transparent overlay with live clock
- `examples/interactive.html` — mouse/keyboard interaction test (cursor tracking, click dots, scroll resize, key display)

Graphs:

- `graphs/browser_hello.json` — loads `hello.html` into a Browser operator and pipes to video output
- `graphs/browser_webgl.json` — loads `webgl-cube.html` (demonstrates the WebGL limitation)

### Graph Format

String params (like `url`) go inside the `"params"` object alongside numeric params — the Vivid graph parser separates strings from numbers by type. There is no separate `"string_params"` key at the node level.

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

### WebGL doesn't work

`--disable-gpu` is required in single-process mode, which prevents GL context creation inside the browser. CSS/HTML/JS rendering works fine — only WebGL and WebGPU content won't render.

### Architecture: single-process mode

CEF runs with `--single-process` because Mach port rendezvous IPC fails when CEF is loaded as a `dlopen`'d plugin rather than the main executable. This means no GPU subprocess, no network service subprocess, and no renderer subprocess — everything runs on the main thread. The `Cannot use V8 Proxy resolver in single process mode` warning is benign.

### Historical: CEF re-initialization crash

CEF cannot be initialized, shut down, and re-initialized in the same process. Vivid's plugin probing (`scan_deferred`) opens each dylib, calls `vivid_descriptor()`, then `dlclose`s it. When the dylib was later reopened for real use, `CefInitialize()` would crash with SIGSEGV.

**Fix:** `CefManager::acquire()` was moved out of the `BrowserOp` constructor into `process()`, so CEF is only initialized when the operator actually runs — not during descriptor probing.

## Other Limitations

- **CPU pixel path** — CEF's OnAcceleratedPaint with shared textures is not available on macOS. Pixel upload is ~0.5-1ms for 720p on Apple Silicon.
- **No WebGL/WebGPU** — GPU is disabled in single-process mode, so GPU-accelerated web content won't render.

## Roadmap

- Audio output from the browser (route CEF audio to a Vivid audio output port)
