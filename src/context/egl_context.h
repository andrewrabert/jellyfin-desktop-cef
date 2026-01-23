#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>  // For GL_BGRA_EXT
#include <SDL3/SDL.h>

// Define BGRA if not available
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

class EGLContext_ {
public:
    EGLContext_();
    ~EGLContext_();

    bool init(SDL_Window* window);
    void cleanup();
    void swapBuffers();
    bool resize(int width, int height);

    EGLDisplay display() const { return display_; }
    EGLContext context() const { return context_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig config_ = nullptr;
    struct wl_egl_window* egl_window_ = nullptr;  // Only used for Wayland

    int width_ = 0;
    int height_ = 0;
    bool is_wayland_ = false;
};
