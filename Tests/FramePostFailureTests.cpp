#include <GLES2/gl2.h>
#include "Bridge.h"
#include "FrameBuffer.h"
#include "GLESv2Dispatch.h"
#include <vector>
#include <cstdio>
static GLenum injectedError = GL_NO_ERROR;
static void failedRead(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) {
    injectedError = GL_INVALID_OPERATION;
}
static GLenum readError() { GLenum e = injectedError; injectedError = GL_NO_ERROR; return e; }
int main() {
    if (!ae_gpu_init(64, 64)) return 1;
    FrameBuffer *fb = FrameBuffer::getFB();
    auto color = fb->createColorBuffer(64, 64, GL_RGBA);
    if (!color) return 2;
    auto read = s_gles2.glReadPixels;
    auto error = s_gles2.glGetError;
    s_gles2.glReadPixels = failedRead;
    s_gles2.glGetError = readError;
    bool posted = fb->post(color);
    s_gles2.glReadPixels = read;
    s_gles2.glGetError = error;
    std::vector<uint8_t> pixels(64*64*4);
    if (posted || ae_gpu_frame(pixels.data(), pixels.size())) return 3;
    if (!fb->post(color) || !ae_gpu_frame(pixels.data(), pixels.size())) return 4;
    std::puts("Failed readback suppressed; subsequent valid frame delivered");
}
