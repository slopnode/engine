#pragma once

namespace slopengine {

// Creates a real (non-stub) OpenGL context with no window system involved at
// all: EGL against the EGL_MESA_platform_surfaceless platform, wired into
// rlgl directly. Used by sloprad instead of raylib's InitWindow()/GLFW, since
// sloprad never draws to a screen and only needs a context for texture
// uploads/readback and GPU compute radiosity.
//
// Returns false (with a reason on stderr) if no usable context could be made,
// e.g. libEGL isn't present or the driver doesn't support surfaceless
// contexts.
bool InitHeadlessGLContext(int width, int height);

// Tears down whatever InitHeadlessGLContext() set up. Safe to call even if
// InitHeadlessGLContext() was never called or failed.
void CloseHeadlessGLContext();

} // namespace slopengine
