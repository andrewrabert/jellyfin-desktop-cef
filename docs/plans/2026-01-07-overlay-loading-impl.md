# Overlay Loading UI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the loading/add-server UI a CEF overlay that fades out to reveal the library view underneath.

**Architecture:** Two CEF browsers - overlay (index.html) on top, main (Jellyfin server) below. Compositor blends with alpha. Overlay fades out 1s after main browser starts loading.

**Tech Stack:** CEF, OpenGL compositor, SDL3

---

### Task 1: Add overlay state and second browser variables to main.cpp

**Files:**
- Modify: `src/main.cpp:33-36` (add constants)
- Modify: `src/main.cpp:279-289` (add overlay state variables)

**Step 1: Add overlay fade constants after existing fade constants**

After line 36, add:
```cpp
// Overlay fade constants
constexpr float OVERLAY_FADE_DELAY_SEC = 1.0f;
constexpr float OVERLAY_FADE_DURATION_SEC = 0.25f;
```

**Step 2: Add overlay state variables after cmd_mutex/pending_cmds**

After line 289, add:
```cpp
// Overlay browser state
enum class OverlayState { SHOWING, WAITING, FADING, HIDDEN };
OverlayState overlay_state = OverlayState::SHOWING;
std::chrono::steady_clock::time_point overlay_fade_start;
float overlay_browser_alpha = 1.0f;
std::string pending_server_url;
```

**Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(overlay): add overlay state variables and constants"
```

---

### Task 2: Create OverlayClient class in cef_client.h

**Files:**
- Modify: `src/cef_client.h` (add OverlayClient class)

**Step 1: Add OverlayClient class after Client class**

Before the closing `#endif` or at end of file, add:
```cpp
// Simplified client for overlay browser (no player, no menu)
class OverlayClient : public CefClient, public CefRenderHandler, public CefLifeSpanHandler, public CefDisplayHandler {
public:
    using PaintCallback = std::function<void(const void* buffer, int width, int height)>;
    using LoadServerCallback = std::function<void(const std::string& url)>;

    OverlayClient(int width, int height, PaintCallback on_paint, LoadServerCallback on_load_server);

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) override;

    // CefDisplayHandler
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override;

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    bool isClosed() const { return is_closed_; }
    void resize(int width, int height);
    void sendFocus(bool focused);
    void sendMouseMove(int x, int y, int modifiers);
    void sendMouseClick(int x, int y, bool down, int button, int clickCount, int modifiers);
    void sendKeyEvent(int key, bool down, int modifiers);
    void sendChar(int charCode, int modifiers);

private:
    int width_;
    int height_;
    PaintCallback on_paint_;
    LoadServerCallback on_load_server_;
    std::atomic<bool> is_closed_ = false;
    CefRefPtr<CefBrowser> browser_;

    IMPLEMENT_REFCOUNTING(OverlayClient);
    DISALLOW_COPY_AND_ASSIGN(OverlayClient);
};
```

**Step 2: Commit**

```bash
git add src/cef_client.h
git commit -m "feat(overlay): add OverlayClient class declaration"
```

---

### Task 3: Implement OverlayClient in cef_client.cpp

**Files:**
- Modify: `src/cef_client.cpp` (add OverlayClient implementation)

**Step 1: Add OverlayClient implementation at end of file**

```cpp
// OverlayClient implementation
OverlayClient::OverlayClient(int width, int height, PaintCallback on_paint, LoadServerCallback on_load_server)
    : width_(width), height_(height), on_paint_(std::move(on_paint)), on_load_server_(std::move(on_load_server)) {}

bool OverlayClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                      cef_log_severity_t level,
                                      const CefString& message,
                                      const CefString& source,
                                      int line) {
    std::string levelStr;
    switch (level) {
        case LOGSEVERITY_DEBUG: levelStr = "DEBUG"; break;
        case LOGSEVERITY_INFO: levelStr = "INFO"; break;
        case LOGSEVERITY_WARNING: levelStr = "WARN"; break;
        case LOGSEVERITY_ERROR: levelStr = "ERROR"; break;
        default: levelStr = "LOG"; break;
    }
    std::cerr << "[Overlay:" << levelStr << "] " << message.ToString() << std::endl;
    return false;
}

bool OverlayClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                              CefRefPtr<CefFrame> frame,
                                              CefProcessId source_process,
                                              CefRefPtr<CefProcessMessage> message) {
    std::string name = message->GetName().ToString();
    CefRefPtr<CefListValue> args = message->GetArgumentList();

    std::cerr << "[Overlay IPC] Received: " << name << std::endl;

    if (name == "loadServer" && on_load_server_) {
        std::string url = args->GetString(0).ToString();
        on_load_server_(url);
        return true;
    }

    return false;
}

void OverlayClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    rect.Set(0, 0, width_, height_);
}

void OverlayClient::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                             const RectList& dirtyRects, const void* buffer,
                             int width, int height) {
    if (on_paint_ && type == PET_VIEW) {
        on_paint_(buffer, width, height);
    }
}

void OverlayClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browser_ = browser;
    std::cerr << "Overlay browser created" << std::endl;
}

void OverlayClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    std::cerr << "Overlay browser closing" << std::endl;
    browser_ = nullptr;
    is_closed_ = true;
}

void OverlayClient::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (browser_) {
        browser_->GetHost()->WasResized();
    }
}

void OverlayClient::sendFocus(bool focused) {
    if (browser_) browser_->GetHost()->SetFocus(focused);
}

void OverlayClient::sendMouseMove(int x, int y, int modifiers) {
    if (!browser_) return;
    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = modifiers;
    browser_->GetHost()->SendMouseMoveEvent(event, false);
}

void OverlayClient::sendMouseClick(int x, int y, bool down, int button, int clickCount, int modifiers) {
    if (!browser_) return;
    CefMouseEvent event;
    event.x = x;
    event.y = y;

    CefBrowserHost::MouseButtonType btn_type;
    switch (button) {
        case 1: btn_type = MBT_LEFT; if (down) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON; break;
        case 2: btn_type = MBT_MIDDLE; if (down) modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON; break;
        case 3: btn_type = MBT_RIGHT; if (down) modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON; break;
        default: btn_type = MBT_LEFT; if (down) modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON; break;
    }
    event.modifiers = modifiers;
    browser_->GetHost()->SendMouseClickEvent(event, btn_type, !down, clickCount);
}

void OverlayClient::sendKeyEvent(int key, bool down, int modifiers) {
    if (!browser_) return;
    CefKeyEvent event;
    event.windows_key_code = key;
    event.native_key_code = key;
    event.modifiers = modifiers;
    event.type = down ? KEYEVENT_KEYDOWN : KEYEVENT_KEYUP;
    browser_->GetHost()->SendKeyEvent(event);

    if (down && key == 0x0D) {
        event.type = KEYEVENT_CHAR;
        event.character = '\r';
        event.unmodified_character = '\r';
        browser_->GetHost()->SendKeyEvent(event);
    }
}

void OverlayClient::sendChar(int charCode, int modifiers) {
    if (!browser_) return;
    CefKeyEvent event;
    event.windows_key_code = charCode;
    event.character = charCode;
    event.unmodified_character = charCode;
    event.type = KEYEVENT_CHAR;
    event.modifiers = modifiers;
    browser_->GetHost()->SendKeyEvent(event);
}
```

**Step 2: Commit**

```bash
git add src/cef_client.cpp
git commit -m "feat(overlay): implement OverlayClient class"
```

---

### Task 4: Add loadServer IPC handler to cef_app.cpp

**Files:**
- Modify: `src/cef_app.cpp:85-87` (add loadServer to jmpNative)

**Step 1: Add loadServer function to jmpNative object**

After line 86 (`setFullscreen`), add:
```cpp
    jmpNative->SetValue("loadServer", CefV8Value::CreateFunction("loadServer", handler), V8_PROPERTY_ATTRIBUTE_READONLY);
```

**Step 2: Add V8 handler for loadServer**

In NativeV8Handler::Execute, before the final `return false;` (around line 843), add:
```cpp
    if (name == "loadServer") {
        if (arguments.size() >= 1 && arguments[0]->IsString()) {
            std::string url = arguments[0]->GetStringValue().ToString();
            std::cerr << "[V8] loadServer: " << url << std::endl;
            CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("loadServer");
            msg->GetArgumentList()->SetString(0, url);
            browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
        return true;
    }
```

**Step 3: Commit**

```bash
git add src/cef_app.cpp
git commit -m "feat(overlay): add loadServer IPC handler"
```

---

### Task 5: Create second compositor instance for overlay

**Files:**
- Modify: `src/main.cpp` (add overlay_compositor variable)

**Step 1: Add second OpenGLCompositor instance**

After existing compositor creation (~line 210), add:
```cpp
    // Second compositor for overlay browser
    OpenGLCompositor overlay_compositor;
    if (!overlay_compositor.init(&egl, width, height, false)) {  // Always software for overlay
        std::cerr << "Overlay compositor init failed" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
```

(Also add for macOS path around line 167)

**Step 2: Commit**

```bash
git add src/main.cpp
git commit -m "feat(overlay): add second compositor instance"
```

---

### Task 6: Create both browsers in main.cpp

**Files:**
- Modify: `src/main.cpp:300-359` (browser creation section)

**Step 1: Create overlay client with paint callback to overlay_compositor**

Before the existing Client creation (line 300), add:
```cpp
    // Overlay browser client (for loading UI)
    CefRefPtr<OverlayClient> overlay_client(new OverlayClient(width, height,
        [&](const void* buffer, int w, int h) {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            void* staging = overlay_compositor.getStagingBuffer(w, h);
            if (staging) {
                memcpy(staging, buffer, w * h * 4);
                overlay_compositor.markStagingDirty();
            }
        },
        [&](const std::string& url) {
            // loadServer callback - start loading main browser
            std::lock_guard<std::mutex> lock(cmd_mutex);
            pending_server_url = url;
        }
    ));
```

**Step 2: Create overlay browser loading index.html**

```cpp
    CefWindowInfo overlay_window_info;
    overlay_window_info.SetAsWindowless(0);
    CefBrowserSettings overlay_browser_settings;
    overlay_browser_settings.background_color = 0;
    overlay_browser_settings.windowless_frame_rate = browser_settings.windowless_frame_rate;

    std::string overlay_html_path = "file://" + (exe_path / "resources" / "index.html").string();
    CefBrowserHost::CreateBrowser(overlay_window_info, overlay_client, overlay_html_path, overlay_browser_settings, nullptr, nullptr);
```

**Step 3: Modify existing Client to use compositor (main browser)**

Keep existing Client but update paint callback to use `compositor` (not overlay_compositor).

**Step 4: Create main browser - load saved server or about:blank**

```cpp
    // Main browser: load saved server immediately, or about:blank
    std::string main_url = Settings::instance().serverUrl();
    if (main_url.empty()) {
        main_url = "about:blank";
    } else {
        // Start fade timer since we're auto-loading
        overlay_state = OverlayState::WAITING;
        overlay_fade_start = Clock::now();
    }
    std::cerr << "Main browser loading: " << main_url << std::endl;
    CefBrowserHost::CreateBrowser(window_info, client, main_url, browser_settings, nullptr, nullptr);
```

**Step 5: Handle pending_server_url in main loop**

In the command processing section, add:
```cpp
        // Check for pending server URL from overlay
        {
            std::lock_guard<std::mutex> lock(cmd_mutex);
            if (!pending_server_url.empty()) {
                std::string url = pending_server_url;
                pending_server_url.clear();

                // Save URL to settings
                Settings::instance().setServerUrl(url);
                Settings::instance().save();

                // Load in main browser
                client->loadUrl(url);  // Need to add this method

                // Start fade timer
                overlay_state = OverlayState::WAITING;
                overlay_fade_start = now;
            }
        }
```

**Step 6: Add loadUrl method to Client**

In cef_client.h/cpp, add:
```cpp
void Client::loadUrl(const std::string& url) {
    if (browser_) {
        browser_->GetMainFrame()->LoadURL(url);
    }
}
```

**Step 7: Commit**

```bash
git add src/main.cpp src/cef_client.h src/cef_client.cpp
git commit -m "feat(overlay): create dual browser setup with loadServer callback"
```

---

### Task 7: Update input routing for dual browsers

**Files:**
- Modify: `src/main.cpp:411-534` (event handling section)

**Step 1: Route input based on overlay state**

Add helper to determine which client receives input:
```cpp
auto getActiveClient = [&]() -> auto {
    if (overlay_state == OverlayState::SHOWING || overlay_state == OverlayState::WAITING) {
        return overlay_client;
    }
    return client;
};
```

**Step 2: Update mouse/keyboard handlers**

Replace `client->sendMouseMove(...)` with `getActiveClient()->sendMouseMove(...)`
(Same for sendMouseClick, sendKeyEvent, sendChar, sendFocus, etc.)

For overlay_client, use the matching methods we added in Task 3.

**Step 3: Update focus handling**

At start of loop, send focus to correct browser:
```cpp
        if (!focus_set) {
            if (overlay_state == OverlayState::SHOWING || overlay_state == OverlayState::WAITING) {
                overlay_client->sendFocus(true);
            } else {
                client->sendFocus(true);
            }
            focus_set = true;
        }
```

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(overlay): route input to correct browser based on state"
```

---

### Task 8: Update rendering for dual browsers

**Files:**
- Modify: `src/main.cpp:656-676` (rendering section)

**Step 1: Add overlay fade state machine**

After processing commands (around line 593), add:
```cpp
        // Update overlay state machine
        if (overlay_state == OverlayState::WAITING) {
            auto elapsed = std::chrono::duration<float>(now - overlay_fade_start).count();
            if (elapsed >= OVERLAY_FADE_DELAY_SEC) {
                overlay_state = OverlayState::FADING;
                overlay_fade_start = now;
            }
        } else if (overlay_state == OverlayState::FADING) {
            auto elapsed = std::chrono::duration<float>(now - overlay_fade_start).count();
            float progress = elapsed / OVERLAY_FADE_DURATION_SEC;
            if (progress >= 1.0f) {
                overlay_browser_alpha = 0.0f;
                overlay_state = OverlayState::HIDDEN;
            } else {
                overlay_browser_alpha = 1.0f - progress;
            }
        }
```

**Step 2: Composite both browsers**

After main compositor flush/composite, add overlay composite:
```cpp
        // Composite main browser (always full opacity)
        compositor.flushOverlay();
        if (compositor.hasValidOverlay()) {
            compositor.composite(current_width, current_height, 1.0f);
        }

        // Composite overlay browser (with fade alpha)
        if (overlay_state != OverlayState::HIDDEN) {
            overlay_compositor.flushOverlay();
            if (overlay_compositor.hasValidOverlay()) {
                overlay_compositor.composite(current_width, current_height, overlay_browser_alpha);
            }
        }
```

**Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(overlay): composite both browsers with fade state machine"
```

---

### Task 9: Update find-webclient.js to use loadServer

**Files:**
- Modify: `resources/find-webclient.js:25-30` (remove window.location, use loadServer)

**Step 1: Replace navigation with IPC call**

Change:
```javascript
        // Fade out page before navigating
        document.body.classList.add('fade-out');
        await new Promise(r => setTimeout(r, 250));

        // Navigation will clean up handlers, but do it explicitly
        window.location = resolvedUrl;
```

To:
```javascript
        // Tell native to load this server in main browser
        if (window.jmpNative && window.jmpNative.loadServer) {
            window.jmpNative.loadServer(resolvedUrl);
        }
```

**Step 2: Remove connectivityHelper.js check (optional simplification)**

For now, keep the connectivity check but it's optional since we're loading directly.

**Step 3: Commit**

```bash
git add resources/find-webclient.js
git commit -m "feat(overlay): use loadServer IPC instead of window.location"
```

---

### Task 10: Update resize and cleanup for both browsers

**Files:**
- Modify: `src/main.cpp:483-514` (resize handling)
- Modify: `src/main.cpp:680-700` (cleanup section)

**Step 1: Resize both clients and compositors on window resize**

In the resize handler, add:
```cpp
                overlay_client->resize(current_width, current_height);
                overlay_compositor.resize(current_width, current_height);
```

**Step 2: Add overlay_compositor cleanup**

In the cleanup section (before CefShutdown), add:
```cpp
    overlay_compositor.cleanup();
```

**Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(overlay): resize and cleanup both browsers"
```

---

### Task 11: Test and verify

**Step 1: Build**

```bash
cmake --build build
```

**Step 2: Test with no saved server**

Run app, verify overlay shows add-server form, enter server, verify library loads behind and overlay fades.

**Step 3: Test with saved server**

Run app again, verify overlay shows spinner, library loads behind, overlay fades after 1s.

**Step 4: Test resize**

Resize window during various states, verify both browsers resize correctly.
