#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class VideoRenderer;

// Runs video rendering on dedicated thread to avoid blocking main loop
class VideoRenderThread {
public:
    VideoRenderThread() = default;
    ~VideoRenderThread();

    void start(VideoRenderer* renderer);
    void stop();

    // Set target dimensions for rendering (thread-safe)
    void setDimensions(int width, int height);

    // Request resize (executed on render thread before next frame)
    void requestResize(int width, int height);

    // Request colorspace setup (executed on render thread)
    void requestSetColorspace() { colorspace_pending_.store(true); notify(); }

    // Enable/disable rendering loop
    void setActive(bool active) {
        active_.store(active);
        if (active) notify();  // Wake thread when activated
    }

    // Wake thread to check for new frames (called from mpv redraw callback)
    void notify() { frame_notified_.store(true); cv_.notify_one(); }

    // Query if video has been rendered at least once
    bool isVideoReady() const { return video_ready_.load(); }

    // Reset video ready state (e.g., when stopping video)
    void resetVideoReady() { video_ready_.store(false); }

private:
    void threadFunc();

    VideoRenderer* renderer_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> video_ready_{false};
    std::atomic<bool> colorspace_pending_{false};
    std::atomic<bool> frame_notified_{false};

    // Dimensions - updated atomically by main thread, read by video thread
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};

    // Resize request (protected by resize_mutex_)
    std::mutex resize_mutex_;
    std::atomic<bool> resize_pending_{false};
    int resize_width_ = 0;
    int resize_height_ = 0;

    // Frame ready notification
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};
