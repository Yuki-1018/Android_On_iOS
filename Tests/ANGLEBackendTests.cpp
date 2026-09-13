// Exercise the iOS resolver on the host without an Apple SDK or real ANGLE.
#include "EGLDispatch.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

extern bool ae_backend_init();
static std::string error;
static const char *missing = nullptr;
static unsigned calls = 0;
static void unusedFunction() { std::abort(); }
extern "C" void ae_gpu_set_error(const char *message) { error = message; }
// Model the broken libEGL shim: entering it would call its unloaded dispatch.
extern "C" __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *) {
    std::abort();
}
// Model the directly linked implementation exported by pinned libGLESv2.
extern "C" __eglMustCastToProperFunctionPointerType EGLAPIENTRY EGL_GetProcAddress(const char *name) {
    ++calls;
    return missing && !std::strcmp(name, missing) ? nullptr : unusedFunction;
}
int main() {
    if (!ae_backend_init() || calls == 0) return 1;
    for (const char *name : {"eglGetPlatformDisplayEXT", "eglBindAPI", "eglCreateImageKHR",
                            "glGetString", "glCreateShader", "glReadPixels"}) {
        missing = name;
        error.clear();
        if (ae_backend_init() || error.find(name) == std::string::npos) return 2;
    }
    std::cout << "Direct ANGLE resolution and missing-symbol diagnostics passed\n";
}
