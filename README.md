# vivid-cef

`vivid-cef` embeds Chromium inside Vivid so HTML, CSS, JavaScript, and browser audio can participate directly in the graph.

## Preview

![vivid-cef preview](docs/images/preview.png)

## Package docs model

- this `README.md` is the package overview used by the central Vivid docs site
- operator reference pages are generated from source doc block comments in `src/`
- the example graphs under `graphs/` are the active smoke surface

## Operators

- `Browser` — renders a URL or local HTML file into a GPU texture
- `BrowserAudioIn` — pulls captured browser audio into the Vivid audio graph

## Install

```bash
./build/vivid install https://github.com/seethroughlab/vivid-cef.git
```

**Note:** first install downloads CEF binaries and builds the wrapper library, so it takes longer than a typical package install.

## Local development

From vivid-core:

```bash
./build/vivid link ../vivid-cef
./build/vivid rebuild vivid-cef
```

## Active example graphs

- `graphs/browser_hello.json`
- `graphs/browser_audio.json`
- `graphs/browser_audio_element.json`
- `graphs/browser_webgl.json`
- `graphs/interactive_demo.json`

## Current contract

- `Browser` outputs a `TEXTURE`.
- `BrowserAudioIn` outputs `AUDIO_BUFFER` stereo audio.
- String params such as `url` live inside the node `params` object.

## Testing

### Deterministic tests

```bash
cmake -B build -S .   -DVIVID_SRC_DIR=/path/to/vivid   -DVIVID_BUILD_DIR=/path/to/vivid/build
cmake --build build --target test_browser_cef_gate test_browser_audio_bridge
ctest --test-dir build --output-on-failure -R vivid_cef_test_
```

### Smoke parity

The package smoke workflow builds the deterministic `vivid_cef_test_*` binaries before running demo-graph smoke coverage.

## Known limitations

- WebGL and WebGPU content still do not render in the current single-process configuration.
- Browser audio supports one consumer per `stream_id`.
- The package currently relies on a CPU pixel upload path for browser frames.

## License

MIT.
