#pragma once
/**
 * @file ZeGLApi.h
 * @brief Platform split for the GL function header -- glad on desktop, system GLES3 on Android
 *
 * @details
 * Every place in the library that calls `glXxx` directly should include this file instead of
 * including `#include <glad/glad.h>` itself. Reasons:
 *
 * - Desktop GL (Windows / Linux): functions are loaded at runtime; glad handles the
 *   `wglGetProcAddress` / `glXGetProcAddress` machinery -- without it, entry points added
 *   after 3.0 such as `glGenVertexArrays` cannot be obtained;
 *
 * - Android / HarmonyOS (GLES 3.x): functions are normal exported symbols of `libGLESv3.so`,
 *   available at link time, and need no loader. Moreover, glad is generated from desktop GL
 *   headers and does not compile at all on Android / HarmonyOS (missing `GL_MULTISAMPLE`,
 *   missing desktop-only enums).
 *
 * So this file provides one name with two implementations: caller code is identical,
 * only the backing differs between glad and the system headers -- the same split as
 * `ZeVulkanContext.h` uses to keep platform types out of the header.
 *
 * @note After including this file, `GLuint` / `GLenum` / `gl*` can be used as usual.
 *       The only difference to watch for is described in `ZEKIT_GL_IS_GLES` (guard anything
 *       absent on ES with it).
 *
 * @note WARNING: HarmonyOS must take the same path as Android (both enter the GLES branch):
 *       the OHOS sysroot contains only `GLES3/gl3.h` and no desktop GL glad header.
 *       Omitting it falls back to the glad branch and fails to compile.
 */

#if defined(__ANDROID__) || defined(__OHOS__)
#  include <GLES3/gl3.h>
#  define ZEKIT_GL_IS_GLES 1
#else
#  include <glad/glad.h>
#  define ZEKIT_GL_IS_GLES 0
#endif
