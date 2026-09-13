#include <GLES2/gl2.h>
#include "EGLDispatch.h"
#include "GLESv2Dispatch.h"
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <cstring>
extern bool ae_backend_init();
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    try {
        require(ae_backend_init(), "backend init");
        EGLDisplay display = s_egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major, minor;
        require(s_egl.eglInitialize(display, &major, &minor), "eglInitialize");
        require(s_egl.eglBindAPI(EGL_OPENGL_ES_API), "bind ES");
        EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config; EGLint count;
        require(s_egl.eglChooseConfig(display, attributes, &config, 1, &count) && count, "config");
        EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        EGLContext context = s_egl.eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        EGLSurface surface = s_egl.eglCreatePbufferSurface(display, config, size);
        require(s_egl.eglMakeCurrent(display, surface, surface, context), "make current");
        GLuint textures[2], fbo;
        s_gles2.glGenTextures(2, textures);
        s_gles2.glBindTexture(GL_TEXTURE_2D, textures[0]);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        s_gles2.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const uint8_t red[] = {255,0,0,255}, green[] = {0,255,0,255}, blue[] = {0,0,255,255};
        s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
        EGLint imageAttributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        EGLImageKHR image = s_egl.eglCreateImageKHR(display, context, EGL_GL_TEXTURE_2D_KHR,
            reinterpret_cast<EGLClientBuffer>(uintptr_t(textures[0])), imageAttributes);
        require(image != EGL_NO_IMAGE_KHR, "texture 2D EGLImage creation");
        s_gles2.glBindTexture(GL_TEXTURE_2D, textures[1]);
        s_gles2.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        s_gles2.glGenFramebuffers(1, &fbo);
        s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[1], 0);
        require(s_gles2.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "image FBO");
        unsigned readback = 0;
        auto pixel = [&](const uint8_t *expected) {
            uint8_t actual[4] = {};
            s_gles2.glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
            GLenum error = s_gles2.glGetError();
            ++readback;
            if (error != GL_NO_ERROR || std::memcmp(actual, expected, 4)) {
                std::fprintf(stderr, "readback %u: %u,%u,%u,%u expected %u,%u,%u,%u GL error %x\n",
                    readback, actual[0], actual[1], actual[2], actual[3],
                    expected[0], expected[1], expected[2], expected[3], error);
                require(false, "shared image pixel");
            }
        };
        pixel(red);
        s_gles2.glBindTexture(GL_TEXTURE_2D, textures[0]);
        s_gles2.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, green);
        pixel(green);
        // Mesa's same-size redefinition can reuse storage; keep the portable
        // lifetime case resized, and run the strict same-size case on ANGLE.
        const bool sameSize = argc == 2 && !std::strcmp(argv[1], "--same-size");
        uint8_t replacement[16];
        for (unsigned i = 0; i < 4; ++i) std::memcpy(replacement + 4*i, blue, 4);
        s_gles2.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sameSize ? 1 : 2, sameSize ? 1 : 2,
                            0, GL_RGBA, GL_UNSIGNED_BYTE, replacement);
        pixel(green);
        s_gles2.glDeleteTextures(1, textures);
        require(s_egl.eglDestroyImageKHR(display, image), "destroy image handle");
        pixel(green); // Target retains storage even after source and EGL handle disappear.
        s_gles2.glDeleteFramebuffers(1, &fbo);
        s_gles2.glDeleteTextures(1, textures+1);
        s_egl.eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        s_egl.eglDestroyContext(display, context);
        s_egl.eglDestroySurface(display, surface);
        s_egl.eglTerminate(display);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
