#include "opengl_frame_context.h"

#ifdef _WIN32
#include "wgl_context.h"
#include <GL/gl.h>
#else
#include "egl_context.h"
#endif

OpenGLFrameContext::OpenGLFrameContext(GLContext* gl) : gl_(gl) {}

void OpenGLFrameContext::beginFrame(float bg_color, float alpha) {
    glClearColor(bg_color, bg_color, bg_color, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLFrameContext::endFrame() {
    gl_->swapBuffers();
}
