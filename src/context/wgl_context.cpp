#ifdef _WIN32

#include "context/wgl_context.h"
#include <iostream>

WGLContext::WGLContext() = default;

WGLContext::~WGLContext() {
    cleanup();
}

bool WGLContext::init(SDL_Window* window) {
    window_ = window;

    // Get HWND from SDL3
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    hwnd_ = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd_) {
        std::cerr << "[WGL] Failed to get HWND from SDL window" << std::endl;
        return false;
    }

    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        std::cerr << "[WGL] Failed to get DC" << std::endl;
        return false;
    }

    // Set pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc_, &pfd);
    if (!pixelFormat || !SetPixelFormat(hdc_, pixelFormat, &pfd)) {
        std::cerr << "[WGL] Failed to set pixel format" << std::endl;
        return false;
    }

    // Create OpenGL context
    hglrc_ = wglCreateContext(hdc_);
    if (!hglrc_) {
        std::cerr << "[WGL] Failed to create WGL context" << std::endl;
        return false;
    }

    makeCurrent();

    // Get window size
    SDL_GetWindowSize(window, &width_, &height_);

    std::cerr << "[WGL] Context created successfully" << std::endl;
    std::cerr << "[WGL] GL_VERSION: " << glGetString(GL_VERSION) << std::endl;
    std::cerr << "[WGL] GL_RENDERER: " << glGetString(GL_RENDERER) << std::endl;

    return true;
}

void WGLContext::cleanup() {
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
    }
    if (hdc_ && hwnd_) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
    }
}

void WGLContext::makeCurrent() {
    if (hdc_ && hglrc_) {
        wglMakeCurrent(hdc_, hglrc_);
    }
}

void WGLContext::swapBuffers() {
    if (hdc_) {
        SwapBuffers(hdc_);
    }
}

bool WGLContext::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return true;
    }
    width_ = width;
    height_ = height;
    // WGL doesn't need explicit resize handling - the DC is tied to the HWND
    return true;
}

void* WGLContext::getProcAddress(const char* name) {
    // Try wglGetProcAddress first (for extensions)
    void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (proc) {
        return proc;
    }
    // Fall back to GetProcAddress from opengl32.dll (for core GL functions)
    static HMODULE opengl32 = LoadLibraryA("opengl32.dll");
    if (opengl32) {
        return reinterpret_cast<void*>(GetProcAddress(opengl32, name));
    }
    return nullptr;
}

#endif // _WIN32
