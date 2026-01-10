# Jellyfin Desktop CEF

Jellyfin client using CEF for web UI, mpv/libplacebo for video playback with HDR support.

## Supported Platforms

- **Linux** - Wayland only (no X11 support)
- **macOS** - Apple Silicon and Intel

## Architecture

### Components

- **CEF** (Chromium Embedded Framework) - windowless browser for Jellyfin web UI
- **mpv + libplacebo** - gpu-next backend for video with HDR/tone-mapping
- **SDL3** - window management and input
- **Vulkan** - video rendering to Wayland subsurface (Linux) or CAMetalLayer (macOS)
- **OpenGL/EGL** - CEF overlay compositing
- **MPRIS** - D-Bus media controls integration (Linux)

### Rendering Pipeline

```
┌──────────────────────────────────────────────────────────┐
│              Main Event Loop (single thread)             │
│  SDL events │ CEF pump │ mpv events │ MPRIS D-Bus        │
└─────────────────────────┬────────────────────────────────┘
                          │
      ┌───────────────────┼───────────────────┐
      ▼                   ▼                   ▼
┌───────────┐      ┌────────────┐      ┌────────────┐
│  CEF UI   │      │ mpv Player │      │   MPRIS    │
│ (2 browsers)│    │  (Vulkan)  │      │  (D-Bus)   │
│           │      │            │      │            │
│ Overlay   │      │ libmpv     │      │ Metadata   │
│ Main      │      │ gpu-next   │      │ Controls   │
└─────┬─────┘      └──────┬─────┘      └────────────┘
      │                   │
      │ Paint callbacks   │ Render to swapchain
      ▼                   ▼
┌───────────┐      ┌─────────────────┐
│Compositor │      │ Video Subsurface│
│OpenGL/Metal│     │ Wayland/Metal   │
│           │      │ HDR color mgmt  │
└─────┬─────┘      └───────┬─────────┘
      │                    │
      │ Alpha composite    │ Direct scanout
      └─────────┬──────────┘
                ▼
         ┌────────────┐
         │ Presentation│
         │ eglSwapBuffers / CAMetalLayer
         └────────────┘
```

### Key Source Files

| File | Purpose |
|------|---------|
| `main.cpp` | Event loop, SDL handling, rendering orchestration |
| `cef_app.cpp` | CEF init, V8 bindings for JS→native calls |
| `cef_client.cpp` | Browser client, render handler (paint callbacks) |
| `mpv_player_vk.cpp` | libmpv wrapper, playback control |
| `wayland_subsurface.cpp` | Wayland surface hierarchy, HDR color management |
| `opengl_compositor.cpp` | CEF overlay compositing (Linux) |
| `media_session_mpris.cpp` | MPRIS D-Bus integration |

### IPC

**JS → Native**: `window.jmpNative.*` calls (playerLoad, playerSeek, etc.) → V8 handler → command queue → main loop

**Native → JS**: `CefBrowser::SendProcessMessage()` for playback events (playing, paused, finished, position updates)

**mpv → App**: Event callbacks (position, duration, state, finished)

## Building

See [dev/](dev/README.md) for build instructions.

## Options

- `--video <path>` - Load video directly (for testing)
- `--gpu-overlay` - Enable DMA-BUF shared textures for CEF (experimental)
- `--remote-debugging-port=<port>` - Enable Chrome DevTools
