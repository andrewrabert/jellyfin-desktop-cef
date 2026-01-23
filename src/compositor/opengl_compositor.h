#pragma once

#ifdef __APPLE__
#include "context/cgl_context.h"
#include <OpenGL/gl3.h>
typedef CGLContext GLContext;
#elif defined(_WIN32)
#include "context/wgl_context.h"
#include <GL/gl.h>
#include <GL/glext.h>
typedef WGLContext GLContext;
#else
#include "context/egl_context.h"
typedef EGLContext_ GLContext;
#endif

#include <mutex>

class OpenGLCompositor {
public:
    OpenGLCompositor();
    ~OpenGLCompositor();

    bool init(GLContext* ctx, uint32_t width, uint32_t height);
    void cleanup();

    // Update overlay texture from CEF buffer (BGRA) - software path
    void updateOverlay(const void* data, int width, int height);

    // Get direct pointer to staging buffer for zero-copy writes
    void* getStagingBuffer(int width, int height);
    void markStagingDirty() { staging_pending_ = true; }
    bool hasPendingContent() const { return staging_pending_; }

    // Flush pending overlay data to GPU
    bool flushOverlay();

    // Composite overlay to screen with alpha blending
    void composite(uint32_t width, uint32_t height, float alpha);

    // Resize resources
    void resize(uint32_t width, uint32_t height);

    // Set visibility (no-op on Linux, alpha controls rendering)
    void setVisible(bool visible) { (void)visible; }

    // Check if we have valid content to composite
    bool hasValidOverlay() const { return has_content_; }

private:
    bool createTexture();
    bool createShader();
    void destroyTexture();

    GLContext* ctx_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // Overlay texture
    GLuint texture_ = 0;
    bool has_content_ = false;

    // Double-buffered PBOs for async texture upload (software path)
    GLuint pbos_[2] = {0, 0};
    int current_pbo_ = 0;
    void* pbo_mapped_ = nullptr;
    bool staging_pending_ = false;

    // Thread safety
    std::mutex mutex_;

    // Shader program
    GLuint program_ = 0;
    GLint alpha_loc_ = -1;

    // VAO for fullscreen quad
    GLuint vao_ = 0;
};
