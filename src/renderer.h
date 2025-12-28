#pragma once

#include <GL/glew.h>
#include <cstdint>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(int width, int height);
    void updateTexture(const void* buffer, int width, int height);
    void render();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint texture_ = 0;
    GLuint shader_program_ = 0;
    int tex_width_ = 0;
    int tex_height_ = 0;

    GLuint compileShader(GLenum type, const char* source);
    GLuint createShaderProgram();
};
