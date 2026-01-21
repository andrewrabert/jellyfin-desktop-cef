#pragma once

#include "window_state.h"
#include "../player/mpv/mpv_player_gl.h"
#include "../player/mpv/mpv_player_vk.h"

// Window state listener for mpv - pauses on minimize, resumes on restore
// Template to support both MpvPlayerGL and MpvPlayerVk
template<typename MpvPlayer>
class MpvLayerT : public WindowStateListener {
public:
    explicit MpvLayerT(MpvPlayer* mpv) : mpv_(mpv) {}

    void onMinimized() override {
        if (!mpv_) return;
        was_playing_ = mpv_->isPlaying() && !mpv_->isPaused();
        if (was_playing_) {
            mpv_->pause();
        }
    }

    void onRestored() override {
        if (!mpv_) return;
        if (was_playing_) {
            mpv_->play();
            was_playing_ = false;
        }
    }

private:
    MpvPlayer* mpv_ = nullptr;
    bool was_playing_ = false;
};

// Type aliases for convenience
using MpvLayerGL = MpvLayerT<MpvPlayerGL>;
using MpvLayerVk = MpvLayerT<MpvPlayerVk>;

// Default MpvLayer for backward compatibility
#ifdef _WIN32
using MpvLayer = MpvLayerGL;
#elif defined(__APPLE__)
using MpvLayer = MpvLayerVk;
#else
// Linux: Both types available, choose based on display server
using MpvLayer = MpvLayerVk;  // Default
#endif
