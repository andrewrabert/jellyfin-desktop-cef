#pragma once

#include <chrono>
#include "player/media_session.h"

class MacOSMediaBackend : public MediaSessionBackend {
public:
    explicit MacOSMediaBackend(MediaSession* session);
    ~MacOSMediaBackend() override;

    void setMetadata(const MediaMetadata& meta) override;
    void setArtwork(const std::string& dataUri) override;
    void setPlaybackState(PlaybackState state) override;
    void setPosition(int64_t position_us) override;
    void setVolume(double volume) override;
    void setCanGoNext(bool can) override;
    void setCanGoPrevious(bool can) override;
    void setRate(double rate) override;
    void emitSeeked(int64_t position_us) override;
    void update() override;
    int getFd() override { return -1; }  // macOS doesn't use poll fd

    MediaSession* session() { return session_; }

private:
    void updateNowPlayingInfo();

    MediaSession* session_;
    void* delegate_ = nullptr;  // MediaKeysDelegate (Obj-C)
    void* media_remote_lib_ = nullptr;  // dlopen handle for MediaRemote.framework

    MediaMetadata metadata_;
    PlaybackState state_ = PlaybackState::Stopped;
    int64_t position_us_ = 0;
    double rate_ = 1.0;
    bool pending_update_ = false;
    std::chrono::steady_clock::time_point last_position_update_;

    // Private MediaRemote.framework function pointers
    typedef void (*SetNowPlayingVisibilityFunc)(void* origin, int visibility);
    typedef void* (*GetLocalOriginFunc)(void);
    typedef void (*SetCanBeNowPlayingApplicationFunc)(int);
    SetNowPlayingVisibilityFunc SetNowPlayingVisibility_ = nullptr;
    GetLocalOriginFunc GetLocalOrigin_ = nullptr;
    SetCanBeNowPlayingApplicationFunc SetCanBeNowPlayingApplication_ = nullptr;
};

std::unique_ptr<MediaSessionBackend> createMacOSMediaBackend(MediaSession* session);
