#pragma once
#include "frame_context.h"

#ifdef _WIN32
class WGLContext;
using GLContext = WGLContext;
#else
class EGLContext_;
using GLContext = EGLContext_;
#endif

class OpenGLFrameContext : public FrameContext {
public:
    explicit OpenGLFrameContext(GLContext* gl);
    void beginFrame(float bg_color, float alpha) override;
    void endFrame() override;
private:
    GLContext* gl_;
};
