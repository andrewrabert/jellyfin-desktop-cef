#pragma once

class FrameContext {
public:
    virtual ~FrameContext() = default;
    virtual void beginFrame(float bg_color, float alpha) = 0;  // clear with specified alpha
    virtual void endFrame() = 0;  // swap
};
