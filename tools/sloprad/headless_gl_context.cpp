#include "headless_gl_context.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <dlfcn.h>

#include <cstring>

namespace slopengine {

namespace {

using PFN_eglGetProcAddress = decltype(&::eglGetProcAddress);
using PFN_eglGetDisplay = decltype(&::eglGetDisplay);
using PFN_eglQueryString = decltype(&::eglQueryString);
using PFN_eglInitialize = decltype(&::eglInitialize);
using PFN_eglTerminate = decltype(&::eglTerminate);
using PFN_eglBindAPI = decltype(&::eglBindAPI);
using PFN_eglChooseConfig = decltype(&::eglChooseConfig);
using PFN_eglCreateContext = decltype(&::eglCreateContext);
using PFN_eglDestroyContext = decltype(&::eglDestroyContext);
using PFN_eglMakeCurrent = decltype(&::eglMakeCurrent);
using PFN_eglGetError = decltype(&::eglGetError);

struct EglApi {
    PFN_eglGetProcAddress GetProcAddress = nullptr;
    PFN_eglGetDisplay GetDisplay = nullptr;
    PFN_eglQueryString QueryString = nullptr;
    PFN_eglInitialize Initialize = nullptr;
    PFN_eglTerminate Terminate = nullptr;
    PFN_eglBindAPI BindAPI = nullptr;
    PFN_eglChooseConfig ChooseConfig = nullptr;
    PFN_eglCreateContext CreateContext = nullptr;
    PFN_eglDestroyContext DestroyContext = nullptr;
    PFN_eglMakeCurrent MakeCurrent = nullptr;
    PFN_eglGetError GetError = nullptr;
    PFNEGLGETPLATFORMDISPLAYEXTPROC GetPlatformDisplayEXT = nullptr;
};

void* eglLibrary = nullptr;
void* glLibrary = nullptr;
EglApi egl;
EGLDisplay display = EGL_NO_DISPLAY;
EGLContext context = EGL_NO_CONTEXT;
bool rlglReady = false;

// EGL extension strings are space-separated with no internal spaces per
// entry, so a plain substring search needs boundary checks to avoid matching
// "EGL_KHR_create_context" inside "EGL_KHR_create_context_no_error".
bool extensionListed(const char* extensions, const char* name) {
    if (extensions == nullptr) {
        return false;
    }
    const std::size_t nameLen = std::strlen(name);
    const char* cursor = extensions;
    while (const char* match = std::strstr(cursor, name)) {
        const bool startsClean = (match == extensions) || (match[-1] == ' ');
        const char after = match[nameLen];
        if (startsClean && (after == ' ' || after == '\0')) {
            return true;
        }
        cursor = match + nameLen;
    }
    return false;
}

void* loadGLProc(const char* name) {
    if (glLibrary != nullptr) {
        if (void* proc = dlsym(glLibrary, name)) {
            return proc;
        }
    }
    if (egl.GetProcAddress != nullptr) {
        return reinterpret_cast<void*>(egl.GetProcAddress(name));
    }
    return nullptr;
}

} // namespace

bool InitHeadlessGLContext(int width, int height) {
    eglLibrary = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (eglLibrary == nullptr) {
        TraceLog(LOG_ERROR, "sloprad: dlopen(libEGL.so.1) failed: %s", dlerror());
        return false;
    }

    egl.GetProcAddress = reinterpret_cast<PFN_eglGetProcAddress>(dlsym(eglLibrary, "eglGetProcAddress"));
    egl.GetDisplay = reinterpret_cast<PFN_eglGetDisplay>(dlsym(eglLibrary, "eglGetDisplay"));
    egl.QueryString = reinterpret_cast<PFN_eglQueryString>(dlsym(eglLibrary, "eglQueryString"));
    egl.Initialize = reinterpret_cast<PFN_eglInitialize>(dlsym(eglLibrary, "eglInitialize"));
    egl.Terminate = reinterpret_cast<PFN_eglTerminate>(dlsym(eglLibrary, "eglTerminate"));
    egl.BindAPI = reinterpret_cast<PFN_eglBindAPI>(dlsym(eglLibrary, "eglBindAPI"));
    egl.ChooseConfig = reinterpret_cast<PFN_eglChooseConfig>(dlsym(eglLibrary, "eglChooseConfig"));
    egl.CreateContext = reinterpret_cast<PFN_eglCreateContext>(dlsym(eglLibrary, "eglCreateContext"));
    egl.DestroyContext = reinterpret_cast<PFN_eglDestroyContext>(dlsym(eglLibrary, "eglDestroyContext"));
    egl.MakeCurrent = reinterpret_cast<PFN_eglMakeCurrent>(dlsym(eglLibrary, "eglMakeCurrent"));
    egl.GetError = reinterpret_cast<PFN_eglGetError>(dlsym(eglLibrary, "eglGetError"));

    if (egl.GetProcAddress == nullptr || egl.GetDisplay == nullptr || egl.QueryString == nullptr
        || egl.Initialize == nullptr || egl.Terminate == nullptr || egl.BindAPI == nullptr
        || egl.ChooseConfig == nullptr || egl.CreateContext == nullptr || egl.DestroyContext == nullptr
        || egl.MakeCurrent == nullptr || egl.GetError == nullptr) {
        TraceLog(LOG_ERROR, "sloprad: libEGL.so.1 is missing required entry points");
        CloseHeadlessGLContext();
        return false;
    }

    const char* clientExtensions = egl.QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (extensionListed(clientExtensions, "EGL_EXT_platform_base")
        && extensionListed(clientExtensions, "EGL_MESA_platform_surfaceless")) {
        egl.GetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            egl.GetProcAddress("eglGetPlatformDisplayEXT"));
    }

    if (egl.GetPlatformDisplayEXT != nullptr) {
        display = egl.GetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    } else {
        TraceLog(
            LOG_WARNING,
            "sloprad: EGL_MESA_platform_surfaceless not advertised; falling back to eglGetDisplay");
        display = egl.GetDisplay(EGL_DEFAULT_DISPLAY);
    }

    if (display == EGL_NO_DISPLAY) {
        TraceLog(LOG_ERROR, "sloprad: failed to get an EGL display (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (egl.Initialize(display, &major, &minor) == EGL_FALSE) {
        TraceLog(LOG_ERROR, "sloprad: eglInitialize failed (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }
    TraceLog(LOG_INFO, "sloprad: EGL %d.%d surfaceless display ready", static_cast<int>(major), static_cast<int>(minor));

    const char* displayExtensions = egl.QueryString(display, EGL_EXTENSIONS);
    if (!extensionListed(displayExtensions, "EGL_KHR_create_context")) {
        TraceLog(LOG_ERROR, "sloprad: driver is missing EGL_KHR_create_context");
        CloseHeadlessGLContext();
        return false;
    }
    if (!extensionListed(displayExtensions, "EGL_KHR_surfaceless_context")) {
        TraceLog(LOG_ERROR, "sloprad: driver is missing EGL_KHR_surfaceless_context");
        CloseHeadlessGLContext();
        return false;
    }

    if (egl.BindAPI(EGL_OPENGL_API) == EGL_FALSE) {
        TraceLog(LOG_ERROR, "sloprad: eglBindAPI(EGL_OPENGL_API) failed (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (egl.ChooseConfig(display, configAttribs, &config, 1, &configCount) == EGL_FALSE || configCount < 1) {
        TraceLog(LOG_ERROR, "sloprad: eglChooseConfig found no usable config (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        4,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_NONE,
    };
    context = egl.CreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        TraceLog(LOG_ERROR, "sloprad: eglCreateContext(4.3 core) failed (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }

    if (egl.MakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_FALSE) {
        TraceLog(LOG_ERROR, "sloprad: eglMakeCurrent failed (error 0x%x)", egl.GetError());
        CloseHeadlessGLContext();
        return false;
    }

    glLibrary = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (glLibrary == nullptr) {
        TraceLog(
            LOG_WARNING,
            "sloprad: dlopen(libGL.so.1) failed: %s; relying on eglGetProcAddress alone",
            dlerror());
    }

    rlLoadExtensions(reinterpret_cast<void*>(&loadGLProc));

    rlglInit(width, height);
    rlglReady = true;

    using GetErrorFn = unsigned int (*)();
    if (GetErrorFn getError =
            reinterpret_cast<GetErrorFn>(rlGetProcAddress("glGetError"))) {
        while (getError() != 0) {
        }
    }

    return true;
}

void CloseHeadlessGLContext() {
    if (rlglReady) {
        rlglClose();
        rlglReady = false;
    }
    if (display != EGL_NO_DISPLAY && egl.MakeCurrent != nullptr) {
        egl.MakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (context != EGL_NO_CONTEXT && egl.DestroyContext != nullptr) {
        egl.DestroyContext(display, context);
        context = EGL_NO_CONTEXT;
    }
    if (display != EGL_NO_DISPLAY && egl.Terminate != nullptr) {
        egl.Terminate(display);
        display = EGL_NO_DISPLAY;
    }
    glLibrary = nullptr;
    eglLibrary = nullptr;
    egl = EglApi{};
}

} // namespace slopengine
