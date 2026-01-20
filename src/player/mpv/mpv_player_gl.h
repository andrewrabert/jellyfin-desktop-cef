#pragma once

#ifdef _WIN32
#include "context/wgl_context.h"
using GLContext = WGLContext;
#elif defined(__APPLE__)
#include "context/cgl_context.h"
using GLContext = CGLContext;
#else
#include "context/egl_context.h"
using GLContext = EGLContext_;
#endif

#include <string>
#include <functional>
#include <atomic>
#include <vector>

struct mpv_handle;
struct mpv_render_context;

class MpvPlayerGL {
public:
    using RedrawCallback = std::function<void()>;
    using PositionCallback = std::function<void(double ms)>;
    using DurationCallback = std::function<void(double ms)>;
    using StateCallback = std::function<void(bool paused)>;
    using PlaybackCallback = std::function<void()>;
    using SeekCallback = std::function<void(double ms)>;
    using BufferingCallback = std::function<void(bool buffering, double ms)>;
    using CoreIdleCallback = std::function<void(bool idle, double ms)>;
    struct BufferedRange { int64_t start; int64_t end; };
    using BufferedRangesCallback = std::function<void(const std::vector<BufferedRange>&)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    MpvPlayerGL();
    ~MpvPlayerGL();

    bool init(GLContext* gl);
    void cleanup();
    bool loadFile(const std::string& path, double startSeconds = 0.0);

    void processEvents();
    bool hasFrame() const;

    // Render to the default framebuffer (or specified FBO)
    void render(int width, int height, int fbo = 0);

    // Playback control
    void stop();
    void pause();
    void play();
    void seek(double seconds);
    void setVolume(int volume);
    void setMuted(bool muted);
    void setSpeed(double speed);
    void setNormalizationGain(double gainDb);
    void setSubtitleTrack(int sid);
    void setAudioTrack(int aid);
    void setAudioDelay(double seconds);

    // State queries
    double getPosition() const;
    double getDuration() const;
    double getSpeed() const;
    bool isPaused() const;
    bool isPlaying() const { return playing_; }

    void setRedrawCallback(RedrawCallback cb) { redraw_callback_ = cb; }
    bool needsRedraw() const { return needs_redraw_.load(); }
    void clearRedrawFlag() { needs_redraw_ = false; }

    void setPositionCallback(PositionCallback cb) { on_position_ = cb; }
    void setDurationCallback(DurationCallback cb) { on_duration_ = cb; }
    void setStateCallback(StateCallback cb) { on_state_ = cb; }
    void setPlayingCallback(PlaybackCallback cb) { on_playing_ = cb; }
    void setFinishedCallback(PlaybackCallback cb) { on_finished_ = cb; }
    void setCanceledCallback(PlaybackCallback cb) { on_canceled_ = cb; }
    void setSeekedCallback(SeekCallback cb) { on_seeked_ = cb; }
    void setBufferingCallback(BufferingCallback cb) { on_buffering_ = cb; }
    void setCoreIdleCallback(CoreIdleCallback cb) { on_core_idle_ = cb; }
    void setBufferedRangesCallback(BufferedRangesCallback cb) { on_buffered_ranges_ = cb; }
    void setErrorCallback(ErrorCallback cb) { on_error_ = cb; }

    bool isHdr() const { return false; }  // OpenGL path doesn't support HDR

private:
    static void onMpvRedraw(void* ctx);
    static void onMpvWakeup(void* ctx);
    void handleMpvEvent(struct mpv_event* event);

    GLContext* gl_ = nullptr;
    mpv_handle* mpv_ = nullptr;
    mpv_render_context* render_ctx_ = nullptr;

    RedrawCallback redraw_callback_;
    PositionCallback on_position_;
    DurationCallback on_duration_;
    StateCallback on_state_;
    PlaybackCallback on_playing_;
    PlaybackCallback on_finished_;
    PlaybackCallback on_canceled_;
    SeekCallback on_seeked_;
    BufferingCallback on_buffering_;
    CoreIdleCallback on_core_idle_;
    BufferedRangesCallback on_buffered_ranges_;
    ErrorCallback on_error_;

    std::atomic<bool> needs_redraw_{false};
    std::atomic<bool> has_events_{false};
    bool playing_ = false;
    bool seeking_ = false;
    double last_position_ = 0.0;
};
