#pragma once
#include "video_renderer.h"

class MpvPlayerGL;

class OpenGLRenderer : public VideoRenderer {
public:
    explicit OpenGLRenderer(MpvPlayerGL* player);
    bool hasFrame() const override;
    bool render(int width, int height) override;
    void setVisible(bool) override {}           // no-op
    void resize(int, int) override {}           // no-op
    void setDestinationSize(int, int) override {}  // no-op
    void setColorspace() override {}            // no-op
    void cleanup() override {}         // no-op
    float getClearAlpha(bool) const override { return 1.0f; }  // always opaque
    bool isHdr() const override { return false; }
private:
    MpvPlayerGL* player_;
};
