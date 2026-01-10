# MPRIS Integration Design

## Overview

Add MPRIS (Media Player Remote Interfacing Specification) support for Linux, enabling system-level media controls (play/pause, seek, metadata display) via D-Bus.

Design uses platform abstraction layer so macOS (MPNowPlayingInfoCenter) can reuse the same metadata infrastructure.

## Architecture

```
┌─────────────────┐    IPC     ┌──────────────┐
│  Jellyfin Web   │ ────────▶  │  CefClient   │
│  (player.load)  │  metadata  │              │
└─────────────────┘            └──────┬───────┘
                                      │
                                      ▼
                            ┌──────────────────┐
                            │  MediaSession    │  ◀── platform-agnostic
                            │  (metadata +     │      callbacks for control
                            │   playback state)│
                            └────────┬─────────┘
                                     │
                    ┌────────────────┼────────────────┐
                    ▼                ▼                ▼
           ┌──────────────┐  ┌──────────────┐  ┌───────────┐
           │ MprisBackend │  │ MacOSBackend │  │ (future)  │
           │  (sd-bus)    │  │ (MPNowPlaying│  │           │
           │  Linux only  │  │  InfoCenter) │  │           │
           └──────────────┘  └──────────────┘  └───────────┘
```

## MediaSession Interface

```cpp
struct MediaMetadata {
    std::string title;
    std::string artist;
    std::string album;
    int track_number;
    int64_t duration_us;
    std::string art_url;      // Jellyfin URL for fetching
    std::string art_data_uri; // Base64 after fetch
};

enum class PlaybackState { Stopped, Playing, Paused };

class MediaSession {
public:
    // State updates (called from player)
    void setMetadata(const MediaMetadata& meta);
    void setPlaybackState(PlaybackState state);
    void setPosition(int64_t position_us);
    void setVolume(double volume);  // 0.0-1.0

    // Control callbacks (from system → player)
    std::function<void()> onPlay, onPause, onPlayPause, onStop;
    std::function<void(int64_t)> onSeek;
    std::function<void()> onNext, onPrevious;
    std::function<void()> onRaise;
    std::function<void(bool)> onSetFullscreen;
};
```

## MPRIS Backend (Linux)

### D-Bus Registration

- Service: `org.mpris.MediaPlayer2.jellyfin_desktop`
- Object path: `/org/mpris/MediaPlayer2`
- Library: sd-bus (libsystemd)

### Interfaces

**org.mpris.MediaPlayer2 (Root):**

| Property | Type | Value |
|----------|------|-------|
| Identity | string | "Jellyfin Desktop" |
| CanRaise | bool | true |
| CanQuit | bool | false |
| CanSetFullscreen | bool | true |
| Fullscreen | bool | current state |

| Method | Action |
|--------|--------|
| Raise() | SDL_RaiseWindow() |
| Quit() | No-op |

**org.mpris.MediaPlayer2.Player:**

| Property | Type | Notes |
|----------|------|-------|
| PlaybackStatus | string | "Playing" / "Paused" / "Stopped" |
| Metadata | a{sv} | xesam fields |
| Position | int64 | microseconds |
| Volume | double | 0.0-1.0 |
| Rate | double | 1.0 (fixed for now) |
| CanPlay | bool | true when media loaded |
| CanPause | bool | true when playing |
| CanSeek | bool | true when duration known |
| CanGoNext | bool | from Jellyfin queue state |
| CanGoPrevious | bool | from Jellyfin queue state |

| Method | Action |
|--------|--------|
| Play() | playerPlay() |
| Pause() | playerPause() |
| PlayPause() | Toggle based on state |
| Stop() | playerStop() |
| Seek(offset) | playerSeek(current + offset) |
| SetPosition(trackid, pos) | playerSeek(pos) |
| Next() | executeActions(['next']) |
| Previous() | executeActions(['previous']) |

### sd-bus Integration

```cpp
// Connect to session bus
sd_bus_open_user(&bus);
sd_bus_request_name(bus, "org.mpris.MediaPlayer2.jellyfin_desktop", 0);

// Register interfaces
sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2",
    "org.mpris.MediaPlayer2", root_vtable, session);
sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2",
    "org.mpris.MediaPlayer2.Player", player_vtable, session);

// Event loop integration
int bus_fd = sd_bus_get_fd(bus);
// Poll bus_fd alongside SDL events, call sd_bus_process() when readable
```

## Metadata Flow

### IPC Changes

Current signature:
```cpp
playerLoad(url, startMs, audioStream, subtitleStream)
```

New signature:
```cpp
playerLoad(url, startMs, audioStream, subtitleStream, metadataJson)
```

### Jellyfin → MediaMetadata Mapping

| Jellyfin field | MediaMetadata | Notes |
|----------------|---------------|-------|
| Name | title | Always present |
| Artists[0] | artist | For audio |
| SeriesName | artist | For episodes |
| Album | album | For audio |
| SeasonName | album | For episodes |
| IndexNumber | track_number | Episode/track number |
| RunTimeTicks / 10 | duration_us | 100ns → μs |
| Constructed URL | art_url | See below |

### MPRIS Metadata (xesam)

| MediaMetadata | MPRIS field |
|---------------|-------------|
| title | xesam:title |
| artist | xesam:artist (as array) |
| album | xesam:album |
| track_number | xesam:trackNumber |
| duration_us | mpris:length |
| art_data_uri | mpris:artUrl |

### Album Art

1. Construct URL from metadata:
   - Audio: `{server}/Items/{AlbumId}/Images/Primary?tag={AlbumPrimaryImageTag}&maxWidth=300`
   - Episode: `{server}/Items/{SeriesId}/Images/Primary?tag={SeriesPrimaryImageTag}&maxWidth=300`
   - Movie: `{server}/Items/{Id}/Images/Primary?tag={ImageTags.Primary}&maxWidth=300`

2. Fetch async via curl (non-blocking)

3. Convert to base64 data URI: `data:image/jpeg;base64,...`

4. Update MediaSession, emit PropertiesChanged

## Control Flow (System → Player)

| MPRIS Method | MediaSession | CEF IPC |
|--------------|--------------|---------|
| Play() | onPlay() | playerPlay() |
| Pause() | onPause() | playerPause() |
| PlayPause() | onPlayPause() | Toggle |
| Stop() | onStop() | playerStop() |
| Seek(offset) | onSeek(pos) | playerSeek(ms) |
| Next() | onNext() | executeActions(['next']) |
| Previous() | onPrevious() | executeActions(['previous']) |
| Raise() | onRaise() | SDL_RaiseWindow() |

Note: Next/Previous require extending executeActions to support these action names. Jellyfin web handles playlist navigation internally.

## File Structure

```
src/
├── media_session.h          # Platform-agnostic interface
├── media_session.cpp        # Shared logic (metadata parsing)
├── media_session_mpris.h    # Linux MPRIS backend header
├── media_session_mpris.cpp  # sd-bus implementation
└── album_art.h/.cpp         # Async curl fetcher → base64
```

## Build Changes

```cmake
if(LINUX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSTEMD REQUIRED libsystemd)
  target_link_libraries(jellyfin-desktop ${SYSTEMD_LIBRARIES})
  target_sources(jellyfin-desktop PRIVATE
    src/media_session_mpris.cpp)
endif()
```

## Dependencies

- libsystemd (sd-bus) - available on most Linux systems
- libcurl - for album art fetching

## Initialization

1. SDL init
2. Create MediaSession (platform backend auto-selected)
3. Wire callbacks to player control functions
4. Start CEF
5. In event loop: poll sd-bus fd alongside SDL events

## Future: Profile-Based Naming

When profiles are implemented, service name changes to:
```
org.mpris.MediaPlayer2.jellyfin_desktop.profile_{id}
```

This allows multiple instances with separate media controls.
