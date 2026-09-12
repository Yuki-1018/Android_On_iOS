// AndroidEmu: ANGLE supplies actual EGL and both GLES implementations.
#include "EGLDispatch.h"
#include "GLESv1Dispatch.h"
#include "GLESv2Dispatch.h"
#include "NativeSubWindow.h"
#include <dlfcn.h>
#include <cstring>
EGLDispatch s_egl;
gles1_decoder_context_t s_gles1;
gles2_decoder_context_t s_gles2;
#ifdef AE_ANGLE_METAL
extern "C" __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *);
#endif
static void *resolve(const char *name) {
#ifdef AE_ANGLE_METAL
    return reinterpret_cast<void *>(eglGetProcAddress(name));
#else
    static void *egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    static void *gles1 = dlopen("libGLESv1_CM.so.1", RTLD_NOW | RTLD_LOCAL);
    static void *gles2 = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    typedef void *(*GetProc)(const char *);
    static GetProc get = egl ? reinterpret_cast<GetProc>(dlsym(egl, "eglGetProcAddress")) : nullptr;
    void *p = egl ? dlsym(egl, name) : nullptr;
    if (!p && gles2) p = dlsym(gles2, name);
    if (!p && gles1) p = dlsym(gles1, name);
    return p ? p : (get ? get(name) : nullptr);
#endif
}
void *gles1_dispatch_get_proc_func(const char *name, void *) { return resolve(name); }
void *gles2_dispatch_get_proc_func(const char *name, void *) { return resolve(name); }
static EGLDisplay backendDisplay(EGLNativeDisplayType) {
    typedef EGLDisplay (*GetPlatform)(EGLenum, void *, const EGLint *);
    GetPlatform get = reinterpret_cast<GetPlatform>(resolve("eglGetPlatformDisplayEXT"));
#ifdef AE_ANGLE_METAL
    const EGLint attributes[] = {0x3203 /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */, 0x3489 /* METAL */, EGL_NONE};
    return get ? get(0x3202 /* EGL_PLATFORM_ANGLE_ANGLE */, nullptr, attributes) : EGL_NO_DISPLAY;
#else
    return get ? get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, nullptr, nullptr) : EGL_NO_DISPLAY;
#endif
}
bool ae_backend_init() {
    s_egl.eglGetError = reinterpret_cast<eglGetError_t>(resolve("eglGetError"));
    s_egl.eglGetDisplay = reinterpret_cast<eglGetDisplay_t>(resolve("eglGetDisplay"));
    s_egl.eglInitialize = reinterpret_cast<eglInitialize_t>(resolve("eglInitialize"));
    s_egl.eglTerminate = reinterpret_cast<eglTerminate_t>(resolve("eglTerminate"));
    s_egl.eglQueryString = reinterpret_cast<eglQueryString_t>(resolve("eglQueryString"));
    s_egl.eglGetConfigs = reinterpret_cast<eglGetConfigs_t>(resolve("eglGetConfigs"));
    s_egl.eglChooseConfig = reinterpret_cast<eglChooseConfig_t>(resolve("eglChooseConfig"));
    s_egl.eglGetConfigAttrib = reinterpret_cast<eglGetConfigAttrib_t>(resolve("eglGetConfigAttrib"));
    s_egl.eglCreateWindowSurface = reinterpret_cast<eglCreateWindowSurface_t>(resolve("eglCreateWindowSurface"));
    s_egl.eglCreatePbufferSurface = reinterpret_cast<eglCreatePbufferSurface_t>(resolve("eglCreatePbufferSurface"));
    s_egl.eglCreatePixmapSurface = reinterpret_cast<eglCreatePixmapSurface_t>(resolve("eglCreatePixmapSurface"));
    s_egl.eglDestroySurface = reinterpret_cast<eglDestroySurface_t>(resolve("eglDestroySurface"));
    s_egl.eglQuerySurface = reinterpret_cast<eglQuerySurface_t>(resolve("eglQuerySurface"));
    s_egl.eglBindAPI = reinterpret_cast<eglBindAPI_t>(resolve("eglBindAPI"));
    s_egl.eglQueryAPI = reinterpret_cast<eglQueryAPI_t>(resolve("eglQueryAPI"));
    s_egl.eglWaitClient = reinterpret_cast<eglWaitClient_t>(resolve("eglWaitClient"));
    s_egl.eglReleaseThread = reinterpret_cast<eglReleaseThread_t>(resolve("eglReleaseThread"));
    s_egl.eglCreatePbufferFromClientBuffer = reinterpret_cast<eglCreatePbufferFromClientBuffer_t>(resolve("eglCreatePbufferFromClientBuffer"));
    s_egl.eglSurfaceAttrib = reinterpret_cast<eglSurfaceAttrib_t>(resolve("eglSurfaceAttrib"));
    s_egl.eglBindTexImage = reinterpret_cast<eglBindTexImage_t>(resolve("eglBindTexImage"));
    s_egl.eglReleaseTexImage = reinterpret_cast<eglReleaseTexImage_t>(resolve("eglReleaseTexImage"));
    s_egl.eglSwapInterval = reinterpret_cast<eglSwapInterval_t>(resolve("eglSwapInterval"));
    s_egl.eglCreateContext = reinterpret_cast<eglCreateContext_t>(resolve("eglCreateContext"));
    s_egl.eglDestroyContext = reinterpret_cast<eglDestroyContext_t>(resolve("eglDestroyContext"));
    s_egl.eglMakeCurrent = reinterpret_cast<eglMakeCurrent_t>(resolve("eglMakeCurrent"));
    s_egl.eglGetCurrentContext = reinterpret_cast<eglGetCurrentContext_t>(resolve("eglGetCurrentContext"));
    s_egl.eglGetCurrentSurface = reinterpret_cast<eglGetCurrentSurface_t>(resolve("eglGetCurrentSurface"));
    s_egl.eglGetCurrentDisplay = reinterpret_cast<eglGetCurrentDisplay_t>(resolve("eglGetCurrentDisplay"));
    s_egl.eglQueryContext = reinterpret_cast<eglQueryContext_t>(resolve("eglQueryContext"));
    s_egl.eglWaitGL = reinterpret_cast<eglWaitGL_t>(resolve("eglWaitGL"));
    s_egl.eglWaitNative = reinterpret_cast<eglWaitNative_t>(resolve("eglWaitNative"));
    s_egl.eglSwapBuffers = reinterpret_cast<eglSwapBuffers_t>(resolve("eglSwapBuffers"));
    s_egl.eglCopyBuffers = reinterpret_cast<eglCopyBuffers_t>(resolve("eglCopyBuffers"));
    s_egl.eglGetProcAddress = reinterpret_cast<eglGetProcAddress_t>(resolve("eglGetProcAddress"));
    s_egl.eglLockSurfaceKHR = reinterpret_cast<eglLockSurfaceKHR_t>(resolve("eglLockSurfaceKHR"));
    s_egl.eglUnlockSurfaceKHR = reinterpret_cast<eglUnlockSurfaceKHR_t>(resolve("eglUnlockSurfaceKHR"));
    s_egl.eglCreateImageKHR = reinterpret_cast<eglCreateImageKHR_t>(resolve("eglCreateImageKHR"));
    s_egl.eglDestroyImageKHR = reinterpret_cast<eglDestroyImageKHR_t>(resolve("eglDestroyImageKHR"));
    s_egl.eglCreateSyncKHR = reinterpret_cast<eglCreateSyncKHR_t>(resolve("eglCreateSyncKHR"));
    s_egl.eglDestroySyncKHR = reinterpret_cast<eglDestroySyncKHR_t>(resolve("eglDestroySyncKHR"));
    s_egl.eglClientWaitSyncKHR = reinterpret_cast<eglClientWaitSyncKHR_t>(resolve("eglClientWaitSyncKHR"));
    s_egl.eglSignalSyncKHR = reinterpret_cast<eglSignalSyncKHR_t>(resolve("eglSignalSyncKHR"));
    s_egl.eglGetSyncAttribKHR = reinterpret_cast<eglGetSyncAttribKHR_t>(resolve("eglGetSyncAttribKHR"));
    s_egl.eglSetSwapRectangleANDROID = reinterpret_cast<eglSetSwapRectangleANDROID_t>(resolve("eglSetSwapRectangleANDROID"));
    s_egl.eglGetDisplay = backendDisplay;
    s_gles1.initDispatchByName(gles1_dispatch_get_proc_func, nullptr);
    s_gles2.initDispatchByName(gles2_dispatch_get_proc_func, nullptr);
    return s_egl.eglInitialize && s_egl.eglCreateContext && s_egl.eglCreateImageKHR &&
           s_gles1.glGetString && s_gles2.glCreateShader && s_gles2.glEGLImageTargetTexture2DOES;
}
// This embedding uses pbuffers and post callbacks, never a desktop subwindow.
EGLNativeWindowType createSubWindow(FBNativeWindowType, int, int, int, int) { return 0; }
void destroySubWindow(EGLNativeWindowType) {}
