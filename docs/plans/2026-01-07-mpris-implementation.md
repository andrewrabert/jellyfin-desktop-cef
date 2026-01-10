# MPRIS Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add MPRIS support for system media controls on Linux via sd-bus.

**Architecture:** Platform-agnostic MediaSession abstraction with Linux MPRIS backend. Metadata flows from Jellyfin web → CEF IPC → MediaSession → D-Bus.

**Tech Stack:** C++17, sd-bus (libsystemd), libcurl (album art)

---

## Task 1: MediaSession Interface

**Files:**
- Create: `src/media_session.h`

**Step 1: Create header with interface definition**

```cpp
#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include <memory>

struct MediaMetadata {
    std::string title;
    std::string artist;
    std::string album;
    int track_number = 0;
    int64_t duration_us = 0;
    std::string art_url;       // Jellyfin URL
    std::string art_data_uri;  // base64 data URI after fetch
};

enum class PlaybackState { Stopped, Playing, Paused };

class MediaSessionBackend {
public:
    virtual ~MediaSessionBackend() = default;
    virtual void setMetadata(const MediaMetadata& meta) = 0;
    virtual void setPlaybackState(PlaybackState state) = 0;
    virtual void setPosition(int64_t position_us) = 0;
    virtual void setVolume(double volume) = 0;
    virtual void setCanGoNext(bool can) = 0;
    virtual void setCanGoPrevious(bool can) = 0;
    virtual void update() = 0;  // Called from event loop to process events
    virtual int getFd() = 0;    // File descriptor for poll, -1 if none
};

class MediaSession {
public:
    MediaSession();
    ~MediaSession();

    void setMetadata(const MediaMetadata& meta);
    void setPlaybackState(PlaybackState state);
    void setPosition(int64_t position_us);
    void setVolume(double volume);
    void setCanGoNext(bool can);
    void setCanGoPrevious(bool can);

    // Called from event loop
    void update();
    int getFd();

    // Control callbacks (set by main.cpp)
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onPlayPause;
    std::function<void()> onStop;
    std::function<void(int64_t)> onSeek;  // position in microseconds
    std::function<void()> onNext;
    std::function<void()> onPrevious;
    std::function<void()> onRaise;
    std::function<void(bool)> onSetFullscreen;

    // Backend access (for platform-specific callbacks)
    MediaSessionBackend* backend() { return backend_.get(); }

private:
    std::unique_ptr<MediaSessionBackend> backend_;
    PlaybackState state_ = PlaybackState::Stopped;
};
```

**Step 2: Commit**

```bash
git add src/media_session.h
git commit -m "feat(mpris): add MediaSession interface"
```

---

## Task 2: MediaSession Stub Implementation

**Files:**
- Create: `src/media_session.cpp`

**Step 1: Create stub implementation (no backend yet)**

```cpp
#include "media_session.h"

#ifdef __linux__
// Forward declaration - implemented in media_session_mpris.cpp
std::unique_ptr<MediaSessionBackend> createMprisBackend(MediaSession* session);
#endif

MediaSession::MediaSession() {
#ifdef __linux__
    backend_ = createMprisBackend(this);
#endif
}

MediaSession::~MediaSession() = default;

void MediaSession::setMetadata(const MediaMetadata& meta) {
    if (backend_) backend_->setMetadata(meta);
}

void MediaSession::setPlaybackState(PlaybackState state) {
    state_ = state;
    if (backend_) backend_->setPlaybackState(state);
}

void MediaSession::setPosition(int64_t position_us) {
    if (backend_) backend_->setPosition(position_us);
}

void MediaSession::setVolume(double volume) {
    if (backend_) backend_->setVolume(volume);
}

void MediaSession::setCanGoNext(bool can) {
    if (backend_) backend_->setCanGoNext(can);
}

void MediaSession::setCanGoPrevious(bool can) {
    if (backend_) backend_->setCanGoPrevious(can);
}

void MediaSession::update() {
    if (backend_) backend_->update();
}

int MediaSession::getFd() {
    return backend_ ? backend_->getFd() : -1;
}
```

**Step 2: Commit**

```bash
git add src/media_session.cpp
git commit -m "feat(mpris): add MediaSession stub implementation"
```

---

## Task 3: MPRIS Backend - Skeleton

**Files:**
- Create: `src/media_session_mpris.h`
- Create: `src/media_session_mpris.cpp`

**Step 1: Create MPRIS backend header**

```cpp
#pragma once

#include "media_session.h"
#include <systemd/sd-bus.h>

class MprisBackend : public MediaSessionBackend {
public:
    explicit MprisBackend(MediaSession* session);
    ~MprisBackend() override;

    void setMetadata(const MediaMetadata& meta) override;
    void setPlaybackState(PlaybackState state) override;
    void setPosition(int64_t position_us) override;
    void setVolume(double volume) override;
    void setCanGoNext(bool can) override;
    void setCanGoPrevious(bool can) override;
    void update() override;
    int getFd() override;

    // Property getters (called from D-Bus vtable)
    const char* getPlaybackStatus() const;
    int64_t getPosition() const { return position_us_; }
    double getVolume() const { return volume_; }
    bool canGoNext() const { return can_go_next_; }
    bool canGoPrevious() const { return can_go_previous_; }
    const MediaMetadata& getMetadata() const { return metadata_; }

private:
    void emitPropertiesChanged(const char* interface, const char* property);

    MediaSession* session_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* slot_root_ = nullptr;
    sd_bus_slot* slot_player_ = nullptr;

    MediaMetadata metadata_;
    PlaybackState state_ = PlaybackState::Stopped;
    int64_t position_us_ = 0;
    double volume_ = 1.0;
    bool can_go_next_ = false;
    bool can_go_previous_ = false;
};

std::unique_ptr<MediaSessionBackend> createMprisBackend(MediaSession* session);
```

**Step 2: Create MPRIS backend skeleton**

```cpp
#include "media_session_mpris.h"
#include <iostream>
#include <cstring>

// D-Bus object path
static const char* MPRIS_PATH = "/org/mpris/MediaPlayer2";
static const char* MPRIS_ROOT_IFACE = "org.mpris.MediaPlayer2";
static const char* MPRIS_PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";
static const char* SERVICE_NAME = "org.mpris.MediaPlayer2.jellyfin_desktop";

MprisBackend::MprisBackend(MediaSession* session) : session_(session) {
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        std::cerr << "[MPRIS] Failed to connect to session bus: " << strerror(-r) << std::endl;
        return;
    }

    r = sd_bus_request_name(bus_, SERVICE_NAME, 0);
    if (r < 0) {
        std::cerr << "[MPRIS] Failed to acquire service name: " << strerror(-r) << std::endl;
        sd_bus_unref(bus_);
        bus_ = nullptr;
        return;
    }

    std::cerr << "[MPRIS] Registered as " << SERVICE_NAME << std::endl;
    // vtables will be added in next task
}

MprisBackend::~MprisBackend() {
    if (slot_player_) sd_bus_slot_unref(slot_player_);
    if (slot_root_) sd_bus_slot_unref(slot_root_);
    if (bus_) {
        sd_bus_release_name(bus_, SERVICE_NAME);
        sd_bus_unref(bus_);
    }
}

void MprisBackend::setMetadata(const MediaMetadata& meta) {
    metadata_ = meta;
    emitPropertiesChanged(MPRIS_PLAYER_IFACE, "Metadata");
}

void MprisBackend::setPlaybackState(PlaybackState state) {
    state_ = state;
    emitPropertiesChanged(MPRIS_PLAYER_IFACE, "PlaybackStatus");
}

void MprisBackend::setPosition(int64_t position_us) {
    position_us_ = position_us;
    // Position is polled, not signaled (per MPRIS spec)
}

void MprisBackend::setVolume(double volume) {
    volume_ = volume;
    emitPropertiesChanged(MPRIS_PLAYER_IFACE, "Volume");
}

void MprisBackend::setCanGoNext(bool can) {
    if (can_go_next_ != can) {
        can_go_next_ = can;
        emitPropertiesChanged(MPRIS_PLAYER_IFACE, "CanGoNext");
    }
}

void MprisBackend::setCanGoPrevious(bool can) {
    if (can_go_previous_ != can) {
        can_go_previous_ = can;
        emitPropertiesChanged(MPRIS_PLAYER_IFACE, "CanGoPrevious");
    }
}

void MprisBackend::update() {
    if (!bus_) return;
    int r;
    do {
        r = sd_bus_process(bus_, nullptr);
    } while (r > 0);
}

int MprisBackend::getFd() {
    return bus_ ? sd_bus_get_fd(bus_) : -1;
}

const char* MprisBackend::getPlaybackStatus() const {
    switch (state_) {
        case PlaybackState::Playing: return "Playing";
        case PlaybackState::Paused: return "Paused";
        default: return "Stopped";
    }
}

void MprisBackend::emitPropertiesChanged(const char* interface, const char* property) {
    if (!bus_) return;
    sd_bus_emit_properties_changed(bus_, MPRIS_PATH, interface, property, nullptr);
}

std::unique_ptr<MediaSessionBackend> createMprisBackend(MediaSession* session) {
    return std::make_unique<MprisBackend>(session);
}
```

**Step 3: Commit**

```bash
git add src/media_session_mpris.h src/media_session_mpris.cpp
git commit -m "feat(mpris): add MPRIS backend skeleton with bus registration"
```

---

## Task 4: MPRIS Root Interface (vtable)

**Files:**
- Modify: `src/media_session_mpris.cpp`

**Step 1: Add root interface vtable handlers**

Add before MprisBackend constructor:

```cpp
// Root interface property getters
static int prop_get_identity(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "s", "Jellyfin Desktop");
}

static int prop_get_can_quit(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "b", false);
}

static int prop_get_can_raise(sd_bus* bus, const char* path, const char* interface,
                              const char* property, sd_bus_message* reply,
                              void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "b", true);
}

static int prop_get_can_set_fullscreen(sd_bus* bus, const char* path, const char* interface,
                                       const char* property, sd_bus_message* reply,
                                       void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "b", true);
}

static int prop_get_fullscreen(sd_bus* bus, const char* path, const char* interface,
                               const char* property, sd_bus_message* reply,
                               void* userdata, sd_bus_error* error) {
    // TODO: track actual fullscreen state
    return sd_bus_message_append(reply, "b", false);
}

static int prop_get_has_track_list(sd_bus* bus, const char* path, const char* interface,
                                   const char* property, sd_bus_message* reply,
                                   void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "b", false);
}

static int prop_get_supported_uri_schemes(sd_bus* bus, const char* path, const char* interface,
                                          const char* property, sd_bus_message* reply,
                                          void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "as", 0);
}

static int prop_get_supported_mime_types(sd_bus* bus, const char* path, const char* interface,
                                         const char* property, sd_bus_message* reply,
                                         void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "as", 0);
}

// Root interface methods
static int method_raise(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    auto* session = backend->session();
    if (session->onRaise) session->onRaise();
    return sd_bus_reply_method_return(m, "");
}

static int method_quit(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable root_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Identity", "s", prop_get_identity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanQuit", "b", prop_get_can_quit, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", prop_get_can_raise, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSetFullscreen", "b", prop_get_can_set_fullscreen, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Fullscreen", "b", prop_get_fullscreen, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("HasTrackList", "b", prop_get_has_track_list, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", prop_get_supported_uri_schemes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", prop_get_supported_mime_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Raise", "", "", method_raise, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", method_quit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};
```

**Step 2: Register vtable in constructor**

Add after `sd_bus_request_name` in constructor:

```cpp
    r = sd_bus_add_object_vtable(bus_, &slot_root_, MPRIS_PATH,
                                  MPRIS_ROOT_IFACE, root_vtable, this);
    if (r < 0) {
        std::cerr << "[MPRIS] Failed to add root vtable: " << strerror(-r) << std::endl;
    }
```

**Step 3: Add session accessor to MprisBackend class**

Add to public section of MprisBackend in header:

```cpp
    MediaSession* session() { return session_; }
```

**Step 4: Commit**

```bash
git add src/media_session_mpris.h src/media_session_mpris.cpp
git commit -m "feat(mpris): add root interface (Identity, Raise)"
```

---

## Task 5: MPRIS Player Interface (vtable)

**Files:**
- Modify: `src/media_session_mpris.cpp`

**Step 1: Add player interface property getters**

Add after root vtable handlers:

```cpp
// Player interface property getters
static int prop_get_playback_status(sd_bus* bus, const char* path, const char* interface,
                                    const char* property, sd_bus_message* reply,
                                    void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    return sd_bus_message_append(reply, "s", backend->getPlaybackStatus());
}

static int prop_get_position(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    return sd_bus_message_append(reply, "x", backend->getPosition());
}

static int prop_get_volume(sd_bus* bus, const char* path, const char* interface,
                           const char* property, sd_bus_message* reply,
                           void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    return sd_bus_message_append(reply, "d", backend->getVolume());
}

static int prop_get_rate(sd_bus* bus, const char* path, const char* interface,
                         const char* property, sd_bus_message* reply,
                         void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "d", 1.0);
}

static int prop_get_min_rate(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "d", 1.0);
}

static int prop_get_max_rate(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "d", 1.0);
}

static int prop_get_can_go_next(sd_bus* bus, const char* path, const char* interface,
                                const char* property, sd_bus_message* reply,
                                void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    return sd_bus_message_append(reply, "b", backend->canGoNext());
}

static int prop_get_can_go_previous(sd_bus* bus, const char* path, const char* interface,
                                    const char* property, sd_bus_message* reply,
                                    void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    return sd_bus_message_append(reply, "b", backend->canGoPrevious());
}

static int prop_get_can_play(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    bool can = backend->getMetadata().duration_us > 0;
    return sd_bus_message_append(reply, "b", can);
}

static int prop_get_can_pause(sd_bus* bus, const char* path, const char* interface,
                              const char* property, sd_bus_message* reply,
                              void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    bool can = backend->getMetadata().duration_us > 0;
    return sd_bus_message_append(reply, "b", can);
}

static int prop_get_can_seek(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    bool can = backend->getMetadata().duration_us > 0;
    return sd_bus_message_append(reply, "b", can);
}

static int prop_get_can_control(sd_bus* bus, const char* path, const char* interface,
                                const char* property, sd_bus_message* reply,
                                void* userdata, sd_bus_error* error) {
    return sd_bus_message_append(reply, "b", true);
}

static int prop_get_metadata(sd_bus* bus, const char* path, const char* interface,
                             const char* property, sd_bus_message* reply,
                             void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    const auto& meta = backend->getMetadata();

    sd_bus_message_open_container(reply, 'a', "{sv}");

    // Track ID (required)
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "mpris:trackid");
    sd_bus_message_open_container(reply, 'v', "o");
    sd_bus_message_append(reply, "o", "/org/jellyfin/track/1");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    // Length
    if (meta.duration_us > 0) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "mpris:length");
        sd_bus_message_open_container(reply, 'v', "x");
        sd_bus_message_append(reply, "x", meta.duration_us);
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    // Title
    if (!meta.title.empty()) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "xesam:title");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", meta.title.c_str());
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    // Artist (as array)
    if (!meta.artist.empty()) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "xesam:artist");
        sd_bus_message_open_container(reply, 'v', "as");
        sd_bus_message_append(reply, "as", 1, meta.artist.c_str());
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    // Album
    if (!meta.album.empty()) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "xesam:album");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", meta.album.c_str());
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    // Track number
    if (meta.track_number > 0) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "xesam:trackNumber");
        sd_bus_message_open_container(reply, 'v', "i");
        sd_bus_message_append(reply, "i", meta.track_number);
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    // Art URL
    if (!meta.art_data_uri.empty()) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "mpris:artUrl");
        sd_bus_message_open_container(reply, 'v', "s");
        sd_bus_message_append(reply, "s", meta.art_data_uri.c_str());
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }

    sd_bus_message_close_container(reply);
    return 0;
}
```

**Step 2: Add player interface methods**

```cpp
// Player interface methods
static int method_play(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onPlay) backend->session()->onPlay();
    return sd_bus_reply_method_return(m, "");
}

static int method_pause(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onPause) backend->session()->onPause();
    return sd_bus_reply_method_return(m, "");
}

static int method_play_pause(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onPlayPause) backend->session()->onPlayPause();
    return sd_bus_reply_method_return(m, "");
}

static int method_stop(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onStop) backend->session()->onStop();
    return sd_bus_reply_method_return(m, "");
}

static int method_next(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onNext) backend->session()->onNext();
    return sd_bus_reply_method_return(m, "");
}

static int method_previous(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    if (backend->session()->onPrevious) backend->session()->onPrevious();
    return sd_bus_reply_method_return(m, "");
}

static int method_seek(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    int64_t offset;
    sd_bus_message_read(m, "x", &offset);
    int64_t new_pos = backend->getPosition() + offset;
    if (new_pos < 0) new_pos = 0;
    if (backend->session()->onSeek) backend->session()->onSeek(new_pos);
    return sd_bus_reply_method_return(m, "");
}

static int method_set_position(sd_bus_message* m, void* userdata, sd_bus_error* error) {
    auto* backend = static_cast<MprisBackend*>(userdata);
    const char* track_id;
    int64_t position;
    sd_bus_message_read(m, "ox", &track_id, &position);
    if (backend->session()->onSeek) backend->session()->onSeek(position);
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable player_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", prop_get_playback_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Rate", "d", prop_get_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MinimumRate", "d", prop_get_min_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", prop_get_max_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Metadata", "a{sv}", prop_get_metadata, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Volume", "d", prop_get_volume, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", prop_get_position, 0, 0),  // No signal for position
    SD_BUS_PROPERTY("CanGoNext", "b", prop_get_can_go_next, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanGoPrevious", "b", prop_get_can_go_previous, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPlay", "b", prop_get_can_play, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPause", "b", prop_get_can_pause, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanSeek", "b", prop_get_can_seek, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanControl", "b", prop_get_can_control, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Play", "", "", method_play, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", method_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", "", "", method_play_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", method_stop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Next", "", "", method_next, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", method_previous, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", method_seek, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", method_set_position, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};
```

**Step 3: Register player vtable in constructor**

Add after root vtable registration:

```cpp
    r = sd_bus_add_object_vtable(bus_, &slot_player_, MPRIS_PATH,
                                  MPRIS_PLAYER_IFACE, player_vtable, this);
    if (r < 0) {
        std::cerr << "[MPRIS] Failed to add player vtable: " << strerror(-r) << std::endl;
    }
```

**Step 4: Commit**

```bash
git add src/media_session_mpris.cpp
git commit -m "feat(mpris): add player interface (Play, Pause, Seek, Metadata)"
```

---

## Task 6: CMake Build Integration

**Files:**
- Modify: `CMakeLists.txt`

**Step 1: Add libsystemd dependency and source files**

Find the Linux else block (around line 58) and add after `pkg_check_modules(WAYLAND`:

```cmake
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
```

Add to `PLATFORM_SOURCES` list:

```cmake
        src/media_session.cpp
        src/media_session_mpris.cpp
```

Add to `PLATFORM_LIBRARIES` list:

```cmake
        ${SYSTEMD_LIBRARIES}
```

Add to `PLATFORM_INCLUDE_DIRS` list:

```cmake
        ${SYSTEMD_INCLUDE_DIRS}
```

**Step 2: Build and verify**

```bash
cmake --build build 2>&1 | head -50
```

Expected: Compiles without errors (may have warnings)

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add libsystemd for MPRIS support"
```

---

## Task 7: Wire MediaSession in main.cpp

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add include**

Add after other includes:

```cpp
#ifdef __linux__
#include "media_session.h"
#endif
```

**Step 2: Create MediaSession instance**

Add after `Settings::instance().load();` (around line 115):

```cpp
#ifdef __linux__
    MediaSession mediaSession;
#endif
```

**Step 3: Wire control callbacks**

Add after MediaSession creation:

```cpp
#ifdef __linux__
    mediaSession.onPlay = [&]() {
        if (player.isLoaded()) player.play();
    };
    mediaSession.onPause = [&]() {
        player.pause();
    };
    mediaSession.onPlayPause = [&]() {
        if (player.isLoaded()) {
            if (player.isPaused()) player.play();
            else player.pause();
        }
    };
    mediaSession.onStop = [&]() {
        player.stop();
    };
    mediaSession.onSeek = [&](int64_t position_us) {
        player.seek(position_us / 1000);  // Convert to ms
    };
    mediaSession.onRaise = [&]() {
        SDL_RaiseWindow(window);
    };
#endif
```

**Step 4: Update MediaSession in event loop**

Find the event loop (around line 469) and add before `SDL_PollEvent`:

```cpp
#ifdef __linux__
            mediaSession.update();
#endif
```

**Step 5: Update playback state on player events**

Find where player state changes are handled and add MediaSession updates. This will be done in Task 9 after IPC is updated.

**Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat(mpris): wire MediaSession callbacks in main loop"
```

---

## Task 8: Test Basic MPRIS Registration

**Step 1: Build**

```bash
cmake --build build
```

**Step 2: Run app and verify D-Bus registration**

In terminal 1:
```bash
./build/jellyfin-desktop
```

In terminal 2:
```bash
busctl --user list | grep jellyfin
```

Expected: `org.mpris.MediaPlayer2.jellyfin_desktop`

**Step 3: Test introspection**

```bash
busctl --user introspect org.mpris.MediaPlayer2.jellyfin_desktop /org/mpris/MediaPlayer2
```

Expected: Shows org.mpris.MediaPlayer2 and org.mpris.MediaPlayer2.Player interfaces

**Step 4: Test with playerctl**

```bash
playerctl -l
```

Expected: Lists `jellyfin_desktop`

---

## Task 9: Update IPC to Pass Metadata

**Files:**
- Modify: `src/cef_app.cpp`
- Modify: `src/cef_client.cpp`
- Modify: `src/cef_client.h`

**Step 1: Update JavaScript API to pass metadata**

In `src/cef_app.cpp`, find `player.load` method (around line 188) and update:

```cpp
            load(url, options, streamdata, audioStream, subtitleStream, callback) {
                console.log('[Native] player.load:', url);
                if (callback) {
                    const onPlaying = () => {
                        this.playing.disconnect(onPlaying);
                        this.error.disconnect(onError);
                        callback();
                    };
                    const onError = () => {
                        this.playing.disconnect(onPlaying);
                        this.error.disconnect(onError);
                        callback();
                    };
                    this.playing.connect(onPlaying);
                    this.error.connect(onError);
                }
                if (window.jmpNative && window.jmpNative.playerLoad) {
                    const metadataJson = streamdata?.metadata ? JSON.stringify(streamdata.metadata) : '{}';
                    window.jmpNative.playerLoad(url, options?.startMilliseconds || 0, audioStream || -1, subtitleStream || -1, metadataJson);
                }
            },
```

**Step 2: Update V8 handler to pass metadata**

In `src/cef_app.cpp`, find `playerLoad` handler (around line 753):

```cpp
    if (name == "playerLoad") {
        std::string url = args->GetString(0).ToString();
        int startMs = args->GetSize() > 1 ? args->GetInt(1) : 0;
        int audioIdx = args->GetSize() > 2 ? args->GetInt(2) : -1;
        int subIdx = args->GetSize() > 3 ? args->GetInt(3) : -1;
        std::string metadataJson = args->GetSize() > 4 ? args->GetString(4).ToString() : "{}";

        std::cerr << "[V8] playerLoad: " << url << " startMs=" << startMs << std::endl;

        CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("playerLoad");
        msg->GetArgumentList()->SetString(0, url);
        msg->GetArgumentList()->SetInt(1, startMs);
        msg->GetArgumentList()->SetInt(2, audioIdx);
        msg->GetArgumentList()->SetInt(3, subIdx);
        msg->GetArgumentList()->SetString(4, metadataJson);
        browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        return true;
    }
```

**Step 3: Update Client to receive metadata**

In `src/cef_client.cpp`, update `playerLoad` handler:

```cpp
    if (name == "playerLoad") {
        std::string url = args->GetString(0).ToString();
        int startMs = args->GetSize() > 1 ? args->GetInt(1) : 0;
        std::string metadataJson = args->GetSize() > 4 ? args->GetString(4).ToString() : "{}";
        on_player_msg_("load", url, startMs, metadataJson);
        return true;
    }
```

**Step 4: Update callback signature in cef_client.h**

Change `PlayerMsgCallback` typedef:

```cpp
using PlayerMsgCallback = std::function<void(const std::string& cmd, const std::string& param, int value, const std::string& metadata)>;
```

Update all other `on_player_msg_` calls to include empty metadata:

```cpp
    on_player_msg_("stop", "", 0, "");
    on_player_msg_("pause", "", 0, "");
    on_player_msg_("play", "", 0, "");
    on_player_msg_("seek", "", ms, "");
    on_player_msg_("volume", "", vol, "");
    on_player_msg_("mute", "", muted ? 1 : 0, "");
    on_player_msg_("fullscreen", "", enable ? 1 : 0, "");
```

**Step 5: Commit**

```bash
git add src/cef_app.cpp src/cef_client.cpp src/cef_client.h
git commit -m "feat(mpris): pass metadata through playerLoad IPC"
```

---

## Task 10: Parse Metadata in main.cpp

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add JSON parsing helper**

Add after includes:

```cpp
#ifdef __linux__
#include "media_session.h"
#include <sstream>

// Simple JSON string value extractor (no external JSON lib needed)
static std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++;  // skip opening quote
    std::string result;
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            if (json[pos] == 'n') result += '\n';
            else if (json[pos] == 't') result += '\t';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

static int64_t jsonGetInt64(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    std::string num;
    while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
        num += json[pos++];
    }
    return num.empty() ? 0 : std::stoll(num);
}

static int jsonGetInt(const std::string& json, const std::string& key) {
    return static_cast<int>(jsonGetInt64(json, key));
}

static MediaMetadata parseJellyfinMetadata(const std::string& json, const std::string& serverUrl) {
    MediaMetadata meta;
    meta.title = jsonGetString(json, "Name");

    std::string type = jsonGetString(json, "Type");
    if (type == "Audio") {
        // For audio, use Artists array (simplified: just first artist)
        size_t artistsPos = json.find("\"Artists\":");
        if (artistsPos != std::string::npos) {
            size_t start = json.find('[', artistsPos);
            size_t end = json.find(']', start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string artistsArray = json.substr(start, end - start + 1);
                size_t firstQuote = artistsArray.find('"');
                if (firstQuote != std::string::npos) {
                    size_t secondQuote = artistsArray.find('"', firstQuote + 1);
                    if (secondQuote != std::string::npos) {
                        meta.artist = artistsArray.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
        }
        meta.album = jsonGetString(json, "Album");
        meta.track_number = jsonGetInt(json, "IndexNumber");
    } else if (type == "Episode") {
        meta.artist = jsonGetString(json, "SeriesName");
        meta.album = jsonGetString(json, "SeasonName");
        meta.track_number = jsonGetInt(json, "IndexNumber");
    }

    // Duration: RunTimeTicks is in 100-nanosecond units
    int64_t ticks = jsonGetInt64(json, "RunTimeTicks");
    meta.duration_us = ticks / 10;  // Convert to microseconds

    // Art URL construction (simplified)
    std::string itemId = jsonGetString(json, "Id");
    if (!itemId.empty() && !serverUrl.empty()) {
        meta.art_url = serverUrl + "/Items/" + itemId + "/Images/Primary?maxWidth=300";
    }

    return meta;
}
#endif
```

**Step 2: Update player message callback**

Find where `on_player_msg_` callback is set and update to parse metadata:

```cpp
    client->SetPlayerMsgCallback([&](const std::string& cmd, const std::string& param, int value, const std::string& metadataJson) {
        // ... existing code ...

        if (cmd == "load") {
#ifdef __linux__
            MediaMetadata meta = parseJellyfinMetadata(metadataJson, Settings::instance().serverUrl());
            std::cerr << "[MPRIS] Metadata: title=" << meta.title << " artist=" << meta.artist << std::endl;
            mediaSession.setMetadata(meta);
            mediaSession.setPlaybackState(PlaybackState::Playing);
#endif
            // ... existing load handling ...
        }
        // ... rest of handlers ...
    });
```

**Step 3: Update playback state on player events**

Add state updates for pause/play/stop:

```cpp
        if (cmd == "pause") {
#ifdef __linux__
            mediaSession.setPlaybackState(PlaybackState::Paused);
#endif
            player.pause();
        } else if (cmd == "play") {
#ifdef __linux__
            mediaSession.setPlaybackState(PlaybackState::Playing);
#endif
            player.play();
        } else if (cmd == "stop") {
#ifdef __linux__
            mediaSession.setPlaybackState(PlaybackState::Stopped);
#endif
            player.stop();
        }
```

**Step 4: Update position periodically**

In the event loop, add position update (e.g., every frame when playing):

```cpp
#ifdef __linux__
        if (player.isPlaying()) {
            mediaSession.setPosition(player.getPositionMs() * 1000);  // ms to us
        }
#endif
```

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(mpris): parse Jellyfin metadata and update MediaSession"
```

---

## Task 11: Integration Test

**Step 1: Build**

```bash
cmake --build build
```

**Step 2: Run and test with playerctl**

Terminal 1:
```bash
./build/jellyfin-desktop
```

Terminal 2 (after starting playback in app):
```bash
playerctl -p jellyfin_desktop metadata
playerctl -p jellyfin_desktop status
playerctl -p jellyfin_desktop play-pause
playerctl -p jellyfin_desktop position
```

**Step 3: Test with KDE/GNOME media controls**

- Media keys (play/pause) should control playback
- System media widget should show metadata

---

## Task 12: Album Art Fetching (Optional Enhancement)

**Files:**
- Create: `src/album_art.h`
- Create: `src/album_art.cpp`

This task adds async album art fetching via libcurl. Can be deferred if basic MPRIS without album art is sufficient for initial release.

**Step 1: Create album art fetcher header**

```cpp
#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

class AlbumArtFetcher {
public:
    using Callback = std::function<void(const std::string& data_uri)>;

    AlbumArtFetcher();
    ~AlbumArtFetcher();

    void fetch(const std::string& url, Callback callback);
    void cancel();

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
};
```

Implementation deferred - basic MPRIS works without album art.

---

## Summary

After completing Tasks 1-11:
- MPRIS service registers on D-Bus
- Play/Pause/Stop/Seek controls work via playerctl and media keys
- Metadata (title, artist, album, duration) displays in system widgets
- Position tracking works

Album art (Task 12) can be added later for full feature parity with original jellyfin-desktop.
