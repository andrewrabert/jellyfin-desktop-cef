#pragma once

class VideoRenderer {
public:
    virtual ~VideoRenderer() = default;

    // Rendering
    virtual bool hasFrame() const = 0;
    virtual bool render(int width, int height) = 0;

    // Subsurface lifecycle (no-op for composite renderers)
    virtual void setVisible(bool visible) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setDestinationSize(int width, int height) = 0;  // HiDPI logical size
    virtual void setColorspace() = 0;
    virtual void cleanup() = 0;

    // For frame clear decision
    virtual float getClearAlpha(bool video_ready) const = 0;

    // HDR query
    virtual bool isHdr() const = 0;
};
