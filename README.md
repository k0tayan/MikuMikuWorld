**English** / [日本語](./README.ja.md)

# MikuMikuWorld
A chart editor and viewer for the mobile rhythm game Project Sekai Colorful Stage feat. Hatsune Miku.

## Features:
- Import and export Sliding Universal Score (\*.sus) files.
- BPM, time signature, and hi-speed adjustment.
- Custom timeline divisions up to 1920.
- Create and use custom note presets.
- Customizable keyboard shortcuts.

## Requirements:
- macOS 10.14 or later.
- GPU supporting OpenGL 3.3.

> This fork is macOS-only. Windows support was dropped during the Cocoa port.

## Build from source:
```bash
cmake -S . -B build
cmake --build build
open build/MikuMikuWorld.app
```
Requires CMake 3.21+ and Xcode command-line tools. GLFW and zlib are fetched automatically via `FetchContent`.

User data (config, layout, presets, auto-saves) is stored under `~/Library/Application Support/MikuMikuWorld/`.

## Offline video renderer
Render a gameplay-style video from a chart file and an audio track. Requires `ffmpeg` on `PATH`.

```bash
MikuMikuWorld.app/Contents/MacOS/MikuMikuWorld --render \
  --score chart.sus \
  --audio bgm.mp3 \
  --out out.mp4 \
  --fps 60 --width 1920 --height 1080
```

Supported score formats: `.mmws`, `.sus`, `.json` / `.json.gz` (Sonolus level data).

Notes, holds, hit effects, background, and note sound effects (tap / flick / trace / tick / hold body loop) are mixed into the output. The note SE track is generated in-process from `res/sound/0N/` and muxed with the BGM via ffmpeg `amix`.

Common overrides (all optional):

| Flag | Meaning |
| --- | --- |
| `--note-speed N` | Override note speed (1..12) |
| `--stage-cover N` | Override stage cover |
| `--bg-brightness N` | Override background brightness |
| `--background path` | Override background image |
| `--no-background` | Disable background |
| `--notes-skin 0\|1` | Override notes skin |
| `--effects-profile N` | Override hit effect profile |
| `--mirror` | Mirror score horizontally |
| `--audio-offset SEC` | Shift BGM relative to chart |
| `--se-profile 0\|1` | Select SE profile |
| `--se-volume X` | SE volume multiplier (default 1.0) |
| `--no-se` | Disable note sound effects |
| `--tail SEC` | Extra tail after last note (default 2.0) |
| `--ffmpeg path` | Override ffmpeg binary |

Run `--render --help` for the full list.

## Screenshot:
![MikuMikuWorld](./docs/screenshot.png)
