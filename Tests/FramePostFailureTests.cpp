#include <GLES2/gl2.h>
#include "Bridge.h"
#include "FrameBuffer.h"
#include "GLESv2Dispatch.h"
#include "EGLDispatch.h"
#include "RenderControl.h"
#include <cstring>
#include <vector>
#include <cstdio>
static GLenum injectedError = GL_NO_ERROR;
static void failedRead(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) {
    injectedError = GL_INVALID_OPERATION;
}
static GLenum readError() { GLenum e = injectedError; injectedError = GL_NO_ERROR; return e; }
static char *hostString(EGLDisplay, EGLint name) {
    static char extensions[] = "EGL_ANDROID_blob_cache EGL_EXT_buffer_age EGL_KHR_image_base EGL_KHR_partial_update EGL_KHR_gl_texture_2d_image EGL_KHR_create_context";
    static char version[] = "1.5";
    return name == EGL_EXTENSIONS ? extensions : version;
}
int main() {
    if (!ae_gpu_init(64, 64)) return 1;
    renderControl_decoder_context_t rc;
    initRenderControlContext(&rc);
    auto query = s_egl.eglQueryString;
    s_egl.eglQueryString = hostString;
    const char *expected = "EGL_KHR_image_base EGL_KHR_gl_texture_2d_image ";
    char extensions[256] = {};
    int required = rc.rcQueryEGLString(EGL_EXTENSIONS, nullptr, 0);
    int written = rc.rcQueryEGLString(EGL_EXTENSIONS, extensions, sizeof(extensions));
    s_egl.eglQueryString = query;
    if (required != -written || written != int(strlen(expected) + 1) || strcmp(extensions, expected)) return 5;
    EGLint major = 0, minor = 0;
    if (!rc.rcGetEGLVersion(&major, &minor) || major != 1 || minor != 4) return 6;
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
