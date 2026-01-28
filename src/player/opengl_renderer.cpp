#include "opengl_renderer.h"
#include "mpv/mpv_player_gl.h"

OpenGLRenderer::OpenGLRenderer(MpvPlayerGL* player) : player_(player) {}

bool OpenGLRenderer::hasFrame() const {
    return player_->hasFrame();
}

bool OpenGLRenderer::render(int width, int height) {
    player_->render(width, height, 0);
    return true;
}
