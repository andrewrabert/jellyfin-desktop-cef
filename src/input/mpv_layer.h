#pragma once

#include "window_state.h"
#include "../player/mpv/mpv_player.h"

// Window state listener for mpv - pauses on minimize, resumes on restore
class MpvLayer : public WindowStateListener {
public:
    explicit MpvLayer(MpvPlayer* mpv) : mpv_(mpv) {}

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
