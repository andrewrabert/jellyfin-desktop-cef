#include "player/macos/media_session_macos.h"
#include <dlfcn.h>
#include <iostream>

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

// macOS private visibility enum from MediaRemote.framework
enum {
    MRNowPlayingClientVisibilityUndefined = 0,
    MRNowPlayingClientVisibilityAlwaysVisible,
    MRNowPlayingClientVisibilityVisibleWhenBackgrounded,
    MRNowPlayingClientVisibilityNeverVisible
};

@interface MediaKeysDelegate : NSObject {
    MacOSMediaBackend* backend;
}
- (instancetype)initWithBackend:(MacOSMediaBackend*)backend;
@end

@implementation MediaKeysDelegate

- (instancetype)initWithBackend:(MacOSMediaBackend*)backend_ {
    self = [super init];
    if (self) {
        backend = backend_;
        MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];

        // Register command handlers
        [center.playCommand addTarget:self action:@selector(handleCommand:)];
        [center.pauseCommand addTarget:self action:@selector(handleCommand:)];
        [center.togglePlayPauseCommand addTarget:self action:@selector(handleCommand:)];
        [center.stopCommand addTarget:self action:@selector(handleCommand:)];
        [center.nextTrackCommand addTarget:self action:@selector(handleCommand:)];
        [center.previousTrackCommand addTarget:self action:@selector(handleCommand:)];
        [center.changePlaybackPositionCommand addTarget:self action:@selector(handleSeek:)];
    }
    return self;
}

- (void)dealloc {
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    [center.playCommand removeTarget:self];
    [center.pauseCommand removeTarget:self];
    [center.togglePlayPauseCommand removeTarget:self];
    [center.stopCommand removeTarget:self];
    [center.nextTrackCommand removeTarget:self];
    [center.previousTrackCommand removeTarget:self];
    [center.changePlaybackPositionCommand removeTarget:self];
}

- (MPRemoteCommandHandlerStatus)handleCommand:(MPRemoteCommandEvent*)event {
    MPRemoteCommand* command = [event command];
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    MediaSession* session = backend->session();

    if (command == center.playCommand) {
        if (session->onPlay) session->onPlay();
    } else if (command == center.pauseCommand) {
        if (session->onPause) session->onPause();
    } else if (command == center.togglePlayPauseCommand) {
        if (session->onPlayPause) session->onPlayPause();
    } else if (command == center.stopCommand) {
        if (session->onStop) session->onStop();
    } else if (command == center.nextTrackCommand) {
        if (session->onNext) session->onNext();
    } else if (command == center.previousTrackCommand) {
        if (session->onPrevious) session->onPrevious();
    } else {
        return MPRemoteCommandHandlerStatusCommandFailed;
    }

    return MPRemoteCommandHandlerStatusSuccess;
}

- (MPRemoteCommandHandlerStatus)handleSeek:(MPChangePlaybackPositionCommandEvent*)event {
    // Update Now Playing position immediately for responsive UI
    // Set rate to 0 to pause progression until mpv finishes seeking
    NSMutableDictionary* info = [NSMutableDictionary
        dictionaryWithDictionary:[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo];
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(event.positionTime);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(0.0);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;

    MediaSession* session = backend->session();
    if (session->onSeek) {
        // Convert seconds to microseconds
        int64_t position_us = static_cast<int64_t>(event.positionTime * 1000000.0);
        session->onSeek(position_us);
    }
    return MPRemoteCommandHandlerStatusSuccess;
}

@end

MacOSMediaBackend::MacOSMediaBackend(MediaSession* session) : session_(session) {
    delegate_ = (__bridge_retained void*)[[MediaKeysDelegate alloc] initWithBackend:this];

    // Load private MediaRemote.framework functions for visibility control
    if (auto lib = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_NOW)) {
        SetNowPlayingVisibility_ = reinterpret_cast<SetNowPlayingVisibilityFunc>(
            dlsym(lib, "MRMediaRemoteSetNowPlayingVisibility"));
        GetLocalOrigin_ = reinterpret_cast<GetLocalOriginFunc>(
            dlsym(lib, "MRMediaRemoteGetLocalOrigin"));
        SetCanBeNowPlayingApplication_ = reinterpret_cast<SetCanBeNowPlayingApplicationFunc>(
            dlsym(lib, "MRMediaRemoteSetCanBeNowPlayingApplication"));

        std::cerr << "[macOS Media] MediaRemote functions: SetVisibility=" << (SetNowPlayingVisibility_ ? "yes" : "no")
                  << " GetOrigin=" << (GetLocalOrigin_ ? "yes" : "no")
                  << " SetCanBe=" << (SetCanBeNowPlayingApplication_ ? "yes" : "no") << std::endl;

        if (SetCanBeNowPlayingApplication_) {
            SetCanBeNowPlayingApplication_(1);
            std::cerr << "[macOS Media] SetCanBeNowPlayingApplication(1) called" << std::endl;
        }
    } else {
        std::cerr << "[macOS Media] Failed to load MediaRemote.framework" << std::endl;
    }

    std::cerr << "[macOS Media] Initialized Now Playing integration" << std::endl;
}

MacOSMediaBackend::~MacOSMediaBackend() {
    // Clear now playing info
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;

    if (delegate_) {
        CFRelease(delegate_);
        delegate_ = nullptr;
    }
}

static MPNowPlayingPlaybackState convertState(PlaybackState state) {
    switch (state) {
        case PlaybackState::Playing: return MPNowPlayingPlaybackStatePlaying;
        case PlaybackState::Paused: return MPNowPlayingPlaybackStatePaused;
        case PlaybackState::Stopped: return MPNowPlayingPlaybackStateStopped;
        default: return MPNowPlayingPlaybackStateUnknown;
    }
}

void MacOSMediaBackend::setMetadata(const MediaMetadata& meta) {
    metadata_ = meta;
    std::cerr << "[macOS Media] setMetadata: " << meta.title << std::endl;
    updateNowPlayingInfo();
}

void MacOSMediaBackend::setArtwork(const std::string& dataUri) {
    metadata_.art_data_uri = dataUri;

    // Parse base64 data URI and create artwork
    // Format: data:image/png;base64,<data>
    size_t comma = dataUri.find(',');
    if (comma == std::string::npos) return;

    std::string base64Data = dataUri.substr(comma + 1);

    // Decode base64
    NSString* nsBase64 = [NSString stringWithUTF8String:base64Data.c_str()];
    NSData* imageData = [[NSData alloc] initWithBase64EncodedString:nsBase64 options:0];
    if (!imageData) return;

    NSImage* image = [[NSImage alloc] initWithData:imageData];
    if (!image) return;

    MPMediaItemArtwork* artwork = [[MPMediaItemArtwork alloc]
        initWithBoundsSize:image.size
        requestHandler:^NSImage* _Nonnull(CGSize size) {
            return image;
        }];

    NSMutableDictionary* info = [NSMutableDictionary
        dictionaryWithDictionary:[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo];
    info[MPMediaItemPropertyArtwork] = artwork;
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
}

void MacOSMediaBackend::setPlaybackState(PlaybackState state) {
    state_ = state;
    const char* stateStr = (state == PlaybackState::Playing) ? "Playing" :
                           (state == PlaybackState::Paused) ? "Paused" : "Stopped";
    std::cerr << "[macOS Media] setPlaybackState: " << stateStr << std::endl;

    // Clear metadata when stopped
    if (state == PlaybackState::Stopped) {
        metadata_ = MediaMetadata{};
        position_us_ = 0;
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    }

    [MPNowPlayingInfoCenter defaultCenter].playbackState = convertState(state);

    // Update visibility using private API
    if (SetNowPlayingVisibility_ && GetLocalOrigin_) {
        void* origin = GetLocalOrigin_();
        std::cerr << "[macOS Media] Setting visibility, origin=" << origin << std::endl;
        if (state == PlaybackState::Stopped) {
            SetNowPlayingVisibility_(origin, MRNowPlayingClientVisibilityNeverVisible);
        } else {
            SetNowPlayingVisibility_(origin, MRNowPlayingClientVisibilityAlwaysVisible);
        }
    }

    pending_update_ = true;
}

void MacOSMediaBackend::setPosition(int64_t position_us) {
    position_us_ = position_us;

    if (pending_update_) {
        NSMutableDictionary* info = [NSMutableDictionary
            dictionaryWithDictionary:[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo];
        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
            @(static_cast<double>(position_us) / 1000000.0);
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
        pending_update_ = false;
    }
}

void MacOSMediaBackend::setVolume(double volume) {
    // macOS Now Playing doesn't have a volume property
}

void MacOSMediaBackend::setCanGoNext(bool can) {
    [MPRemoteCommandCenter sharedCommandCenter].nextTrackCommand.enabled = can;
}

void MacOSMediaBackend::setCanGoPrevious(bool can) {
    [MPRemoteCommandCenter sharedCommandCenter].previousTrackCommand.enabled = can;
}

void MacOSMediaBackend::setRate(double rate) {
    rate_ = rate;
    NSMutableDictionary* info = [NSMutableDictionary
        dictionaryWithDictionary:[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo];
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(rate);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
}

void MacOSMediaBackend::emitSeeked(int64_t position_us) {
    position_us_ = position_us;
    NSMutableDictionary* info = [NSMutableDictionary
        dictionaryWithDictionary:[MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo];
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
        @(static_cast<double>(position_us) / 1000000.0);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
}

void MacOSMediaBackend::update() {
    // macOS doesn't need polling - commands come through the delegate
}

void MacOSMediaBackend::updateNowPlayingInfo() {
    NSMutableDictionary* info = [NSMutableDictionary dictionary];

    // Title
    if (!metadata_.title.empty()) {
        info[MPMediaItemPropertyTitle] =
            [NSString stringWithUTF8String:metadata_.title.c_str()];
    }

    // Artist
    if (!metadata_.artist.empty()) {
        info[MPMediaItemPropertyArtist] =
            [NSString stringWithUTF8String:metadata_.artist.c_str()];
    }

    // Album
    if (!metadata_.album.empty()) {
        info[MPMediaItemPropertyAlbumTitle] =
            [NSString stringWithUTF8String:metadata_.album.c_str()];
    }

    // Duration (convert microseconds to seconds)
    if (metadata_.duration_us > 0) {
        info[MPMediaItemPropertyPlaybackDuration] =
            @(static_cast<double>(metadata_.duration_us) / 1000000.0);
    }

    // Track number
    if (metadata_.track_number > 0) {
        info[MPMediaItemPropertyAlbumTrackNumber] = @(metadata_.track_number);
    }

    // Current position
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
        @(static_cast<double>(position_us_) / 1000000.0);

    // Playback rate
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(rate_);

    // Media type - assume video for now (most Jellyfin content)
    info[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeVideo);

    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
    std::cerr << "[macOS Media] Updated now playing info: " << metadata_.title << std::endl;
}

std::unique_ptr<MediaSessionBackend> createMacOSMediaBackend(MediaSession* session) {
    return std::make_unique<MacOSMediaBackend>(session);
}
