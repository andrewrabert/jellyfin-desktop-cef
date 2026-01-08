# Overlay Loading UI Design

## Overview

Change initial loading and add-server UI to be a CEF-native overlay layer on top of the library view. This enables smooth fade-out transitions that reveal the library underneath instead of fade → black → new page load.

## Architecture

Two CEF browsers, composited:

```
┌─────────────────────────────┐
│     Overlay Browser         │  ← index.html (logo, form, spinner)
│     z-index: top            │     alpha: 1.0 → 0.0 (fade)
├─────────────────────────────┤
│     Main Browser            │  ← Jellyfin server URL (library)
│     z-index: bottom         │     always rendered
└─────────────────────────────┘
```

- Both browsers render to separate textures
- Compositor blends them with overlay alpha
- Overlay destroyed after fade complete

## State Flow

```
APP_START
  ↓
[saved server?]──no──→ OVERLAY_ADD_SERVER (form visible)
  │ yes                      │
  ↓                          │ user submits URL
OVERLAY_LOADING ←────────────┘
  │
  ↓ (main browser starts loading server URL)
WAITING (1s timer)
  │
  ↓ (timer expires)
FADE_OUT_OVERLAY (250ms)
  │
  ↓ (fade complete)
LIBRARY_ACTIVE
```

## Rendering

```cpp
GLuint main_texture;      // library view
GLuint overlay_texture;   // loading UI
float overlay_alpha = 1.0f;

// In render loop:
draw_texture(main_texture, 1.0f);
draw_texture(overlay_texture, overlay_alpha);
```

## Timing

- **Delay before fade:** 1 second after main browser starts loading
- **Fade duration:** 250ms (matches current CSS fade)

## IPC

**Overlay → Main process:**
- `startLoadingServer(url)` - user submitted URL, load in main browser

**Main process → Overlay:**
- `jmpLoadError(message)` - main browser failed to load, show error

## Error Handling

- `OnLoadError` callback on main browser triggers error display in overlay
- Cancel fade timer on error
- User can retry, which restarts the flow

## Files to Modify

1. **`main.cpp`**
   - Add second `CefBrowser` for overlay
   - Add `overlay_texture`, `overlay_alpha` variables
   - Composite both textures in render loop
   - Add fade timer logic

2. **`cef_client.cpp`**
   - Handle two browsers (route events correctly)
   - Add `OnLoadError` handling → notify overlay
   - Add IPC: `startLoadingServer(url)`

3. **`index.html` / `find-webclient.js`**
   - Remove `window.location` navigation
   - Call `window.api.startLoadingServer(url)` instead
   - Remove CSS fade-out

4. **`cef_app.cpp`**
   - Inject `window.api.startLoadingServer()` into overlay browser

## What Gets Removed

- `connectivityHelper.js` - no pre-check needed, load directly
- CSS fade-out animation - handled natively
