#pragma once
/**
 * @file ZeGLProgram.h
 * @brief Compiles a GL shader program with glad -- the small piece that is independent of the window system
 *
 * @details
 * Extracted from `ZeGLContext`. Originally `Kit::UIRenderer::Init()` required a
 * `GL::GLContext&` before it would compile shaders, and `GLContext` is WGL-specific
 * (`<Windows.h>` + `HDC` + `HGLRC`) -- so the "GL UI renderer" chain would not
 * compile at all on Linux.
 *
 * But only one capability is actually used: give two source strings, get a program back.
 * This depends only on glad's `gl*` functions and has nothing to do with the window system -- so it was moved into this file:
 *
 *     GLContext --+
 *                 +--> ZeGLProgram (this file, pure glad)
 *     EglContext -+
 *
 * Thus `Kit::UIRenderer` becomes a single implementation shared by both ends: WGL context on Windows,
 * EGL context on Linux, and the `Init()` call site needs no arguments.
 *
 * @note The prerequisite is that a current GL context already exists -- this file does no context management,
 *       it is just a few `gl*` calls. Whoever calls it is responsible for the context.
 */

#include "ZeGLApi.h"

namespace GL {

    /// @brief Compiles one shader
    /// @return Shader object; 0 = compilation failed (error log already printed to stderr)
    GLuint CompileShader(GLenum type, const char* source);

    /// @brief Compiles and links a program (the two intermediate shaders are deleted internally)
    /// @return Program object; 0 = compilation or linking failed (error log already printed to stderr)
    GLuint CompileProgram(const char* vertSrc, const char* fragSrc);

} // namespace GL
