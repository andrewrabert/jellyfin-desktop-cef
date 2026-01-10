# MPRIS JavaScript Notifications Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable MPRIS to receive metadata, position, and playback state from Jellyfin's web UI via JavaScript event monitoring.

**Architecture:** JavaScript monitors Jellyfin's `playbackManager` via `window.Events.on()` and pushes notifications to C++ via IPC. C++ updates MediaSession which propagates to D-Bus MPRIS interface.

**Tech Stack:** CEF IPC, sd-bus (systemd D-Bus), JavaScript event listeners

---

### Task 1: Add V8 Handler for notifyMetadata IPC

**Files:**
- Modify: `src/cef_app.cpp:761-878` (NativeV8Handler::Execute)

**Step 1: Add notifyMetadata handler after playerSetMuted block (~line 845)**

```cpp
    if (name == "notifyMetadata") {
        if (arguments.size() >= 1 && arguments[0]->IsString()) {
            std::string metadata = arguments[0]->GetStringValue().ToString();
            std::cerr << "[V8] notifyMetadata: " << metadata.substr(0, 100) << "..." << std::endl;
            CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("notifyMetadata");
            msg->GetArgumentList()->SetString(0, metadata);
            browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
        return true;
    }
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors related to notifyMetadata

---

### Task 2: Add V8 Handlers for notifyPosition and notifyPlaybackState

**Files:**
- Modify: `src/cef_app.cpp:761-878` (NativeV8Handler::Execute)

**Step 1: Add notifyPosition handler after notifyMetadata**

```cpp
    if (name == "notifyPosition") {
        if (arguments.size() >= 1 && arguments[0]->IsInt()) {
            int posMs = arguments[0]->GetIntValue();
            CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("notifyPosition");
            msg->GetArgumentList()->SetInt(0, posMs);
            browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
        return true;
    }

    if (name == "notifyPlaybackState") {
        if (arguments.size() >= 1 && arguments[0]->IsString()) {
            std::string state = arguments[0]->GetStringValue().ToString();
            std::cerr << "[V8] notifyPlaybackState: " << state << std::endl;
            CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("notifyPlaybackState");
            msg->GetArgumentList()->SetString(0, state);
            browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
        return true;
    }
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 3: Add JS Native Bindings for Notification Methods

**Files:**
- Modify: `src/cef_app.cpp:91-103` (jmpNative object creation)

**Step 1: Add new methods to jmpNative object (after setFullscreen line ~101)**

```cpp
    jmpNative->SetValue("notifyMetadata", CefV8Value::CreateFunction("notifyMetadata", handler), V8_PROPERTY_ATTRIBUTE_READONLY);
    jmpNative->SetValue("notifyPosition", CefV8Value::CreateFunction("notifyPosition", handler), V8_PROPERTY_ATTRIBUTE_READONLY);
    jmpNative->SetValue("notifyPlaybackState", CefV8Value::CreateFunction("notifyPlaybackState", handler), V8_PROPERTY_ATTRIBUTE_READONLY);
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 4: Add IPC Message Handling in Client

**Files:**
- Modify: `src/cef_client.cpp:52-100` (OnProcessMessageReceived)

**Step 1: Add handlers after setFullscreen block (~line 99)**

```cpp
    } else if (name == "notifyMetadata") {
        std::string metadata = args->GetString(0).ToString();
        on_player_msg_("mpris_metadata", metadata, 0, "");
        return true;
    } else if (name == "notifyPosition") {
        int posMs = args->GetInt(0);
        on_player_msg_("mpris_position", "", posMs, "");
        return true;
    } else if (name == "notifyPlaybackState") {
        std::string state = args->GetString(0).ToString();
        on_player_msg_("mpris_state", state, 0, "");
        return true;
    }
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 5: Handle MPRIS Commands in Main Loop

**Files:**
- Modify: `src/main.cpp:877-950` (command processing loop)

**Step 1: Add MPRIS command handlers after fullscreen block (~line 949)**

```cpp
                } else if (cmd.cmd == "mpris_metadata") {
                    MediaMetadata meta = parseMetadataJson(cmd.arg);
                    std::cerr << "[MAIN] MPRIS metadata: title=" << meta.title << std::endl;
                    mediaSession.setMetadata(meta);
                } else if (cmd.cmd == "mpris_position") {
                    int64_t pos_us = static_cast<int64_t>(cmd.intArg) * 1000;
                    mediaSession.setPosition(pos_us);
                } else if (cmd.cmd == "mpris_state") {
                    if (cmd.arg == "Playing") {
                        mediaSession.setPlaybackState(PlaybackState::Playing);
                    } else if (cmd.arg == "Paused") {
                        mediaSession.setPlaybackState(PlaybackState::Paused);
                    } else {
                        mediaSession.setPlaybackState(PlaybackState::Stopped);
                    }
                }
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 6: Add JavaScript Event Monitoring for playbackManager

**Files:**
- Modify: `src/cef_app.cpp:106-426` (native_shim JS)

**Step 1: Add playback monitoring code at end of native shim (before closing `})();`)**

Insert before line ~425:

```javascript
    // MPRIS: Monitor Jellyfin playback events
    (function setupMprisNotifications() {
        let positionInterval = null;
        let lastPlaybackManager = null;

        function getPlaybackManager() {
            // Try different paths where playbackManager might be
            if (window.ApiClient && window.ApiClient.serverInfo) {
                const pm = window.require ? window.require('playbackManager') : null;
                if (pm) return pm;
            }
            // Fallback: look for global
            if (window.playbackManager) return window.playbackManager;
            return null;
        }

        function notifyMetadata(item) {
            if (!item || !window.jmpNative) return;
            const meta = {
                Name: item.Name || '',
                Type: item.Type || '',
                MediaType: item.MediaType || '',
                SeriesName: item.SeriesName || '',
                SeasonName: item.SeasonName || '',
                Album: item.Album || '',
                Artists: item.Artists || [],
                IndexNumber: item.IndexNumber || 0,
                RunTimeTicks: item.RunTimeTicks || 0,
                Id: item.Id || ''
            };
            window.jmpNative.notifyMetadata(JSON.stringify(meta));
        }

        function startPositionUpdates(pm) {
            if (positionInterval) clearInterval(positionInterval);
            positionInterval = setInterval(() => {
                if (!window.jmpNative) return;
                try {
                    const pos = pm.currentTime ? pm.currentTime() : 0;
                    if (typeof pos === 'number' && pos >= 0) {
                        window.jmpNative.notifyPosition(Math.floor(pos));
                    }
                } catch (e) {}
            }, 500);
        }

        function stopPositionUpdates() {
            if (positionInterval) {
                clearInterval(positionInterval);
                positionInterval = null;
            }
        }

        function trySetupEvents() {
            const pm = getPlaybackManager();
            if (!pm || pm === lastPlaybackManager) return;
            lastPlaybackManager = pm;

            console.log('[MPRIS] Found playbackManager, setting up events');

            if (window.Events) {
                window.Events.on(pm, 'playbackstart', (e, player, state) => {
                    console.log('[MPRIS] playbackstart');
                    if (state && state.NowPlayingItem) {
                        notifyMetadata(state.NowPlayingItem);
                    }
                    if (window.jmpNative) window.jmpNative.notifyPlaybackState('Playing');
                    startPositionUpdates(pm);
                });

                window.Events.on(pm, 'playbackstop', () => {
                    console.log('[MPRIS] playbackstop');
                    if (window.jmpNative) window.jmpNative.notifyPlaybackState('Stopped');
                    stopPositionUpdates();
                });

                window.Events.on(pm, 'pause', () => {
                    console.log('[MPRIS] pause');
                    if (window.jmpNative) window.jmpNative.notifyPlaybackState('Paused');
                });

                window.Events.on(pm, 'unpause', () => {
                    console.log('[MPRIS] unpause');
                    if (window.jmpNative) window.jmpNative.notifyPlaybackState('Playing');
                });

                // Also listen on any active player
                window.Events.on(pm, 'playerchange', () => {
                    const player = pm.getCurrentPlayer ? pm.getCurrentPlayer() : null;
                    if (player) {
                        window.Events.on(player, 'pause', () => {
                            if (window.jmpNative) window.jmpNative.notifyPlaybackState('Paused');
                        });
                        window.Events.on(player, 'playing', () => {
                            if (window.jmpNative) window.jmpNative.notifyPlaybackState('Playing');
                        });
                    }
                });
            }
        }

        // Try immediately and retry periodically until playbackManager is available
        trySetupEvents();
        const setupInterval = setInterval(() => {
            trySetupEvents();
            if (lastPlaybackManager) {
                clearInterval(setupInterval);
                console.log('[MPRIS] Event setup complete');
            }
        }, 1000);

        // Stop after 30s if not found
        setTimeout(() => clearInterval(setupInterval), 30000);
    })();
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 7: Update parseMetadataJson for Jellyfin Format

**Files:**
- Modify: `src/main.cpp:158-200` (parseMetadataJson function)

**Step 1: Update parser to handle Jellyfin's NowPlayingItem format**

Replace the parseMetadataJson function:

```cpp
MediaMetadata parseMetadataJson(const std::string& json) {
    MediaMetadata meta;
    if (json.empty() || json == "{}") return meta;

    // Simple JSON parsing for known fields
    auto getString = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        auto end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    auto getInt = [&](const std::string& key) -> int64_t {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return 0;
        // Parse number
        std::string num;
        while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
            num += json[pos++];
        }
        return num.empty() ? 0 : std::stoll(num);
    };

    auto getFirstArrayString = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":[\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        auto end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    // Title from Name
    meta.title = getString("Name");

    // Type-specific handling
    std::string type = getString("Type");
    std::string mediaType = getString("MediaType");

    if (type == "Episode") {
        // For episodes: artist = series name, album = season name
        meta.artist = getString("SeriesName");
        meta.album = getString("SeasonName");
    } else if (mediaType == "Audio" || type == "Audio") {
        // For music: artist from Artists array, album from Album
        meta.artist = getFirstArrayString("Artists");
        if (meta.artist.empty()) meta.artist = getString("AlbumArtist");
        meta.album = getString("Album");
    } else {
        // Movies/other video: use Name as title, no artist/album
    }

    meta.track_number = static_cast<int>(getInt("IndexNumber"));

    // Duration: Jellyfin uses ticks (100ns), MPRIS uses microseconds
    int64_t ticks = getInt("RunTimeTicks");
    if (ticks > 0) {
        meta.duration_us = ticks / 10;  // ticks to microseconds
    }

    return meta;
}
```

**Step 2: Verify compilation**

Run: `cmake --build build 2>&1 | head -50`
Expected: No errors

---

### Task 8: Integration Test

**Step 1: Build the application**

Run: `cmake --build build`
Expected: Clean build with no errors

**Step 2: Run and test MPRIS**

Run application, start playing media, then in another terminal:

```bash
# Check MPRIS service appears
dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames | grep jellyfin

# Get metadata
dbus-send --session --dest=org.mpris.MediaPlayer2.jellyfin_desktop --type=method_call --print-reply /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'Metadata'

# Get playback status
dbus-send --session --dest=org.mpris.MediaPlayer2.jellyfin_desktop --type=method_call --print-reply /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'PlaybackStatus'

# Get position
dbus-send --session --dest=org.mpris.MediaPlayer2.jellyfin_desktop --type=method_call --print-reply /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'Position'
```

Expected: Metadata shows title/artist, PlaybackStatus shows "Playing", Position updates

**Step 3: Test controls**

```bash
# Test play/pause
dbus-send --session --dest=org.mpris.MediaPlayer2.jellyfin_desktop --type=method_call /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.PlayPause
```

Expected: Playback toggles

---

### Task 9: Commit

**Step 1: Stage and commit**

```bash
git add src/cef_app.cpp src/cef_client.cpp src/main.cpp
git commit -m "feat(mpris): add JS→C++ notifications for metadata/position/state

- Add notifyMetadata/notifyPosition/notifyPlaybackState IPC methods
- Monitor Jellyfin playbackManager events in JS shim
- Update parseMetadataJson for Jellyfin NowPlayingItem format
- Position updates sent every 500ms during playback"
```

---

## Testing Notes

Use `playerctl` for easier testing:
```bash
playerctl -p jellyfin_desktop metadata
playerctl -p jellyfin_desktop status
playerctl -p jellyfin_desktop position
playerctl -p jellyfin_desktop play-pause
```
