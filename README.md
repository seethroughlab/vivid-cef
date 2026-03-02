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

## How It Works

1. CEF renders web content offscreen to a CPU pixel buffer (OnPaint callback)
2. Pixels are uploaded to a WebGPU staging texture via `wgpuQueueWriteTexture`
3. A fullscreen blit pass copies the staging texture to the operator's output

CEF lifecycle is reference-counted: the first Browser operator initializes CEF, subsequent operators share the same CEF process, and CEF shuts down when the last Browser operator is destroyed.

## Requirements

- CMake 3.16+
- macOS (arm64/x86_64), Windows, or Linux
- Internet connection for first build (downloads CEF from Spotify CDN)

## Known Limitations

- **No input forwarding** — mouse/keyboard events are not forwarded to the browser. Many use cases (visualizations, dashboards, WebGL) don't need interaction.
- **CPU pixel path** — CEF's OnAcceleratedPaint with shared textures is not available on macOS. Pixel upload is ~0.5–1ms for 720p on Apple Silicon.
