
#include "pch.h"
#include "ZeGLProgram.h"

#include <iostream>

namespace GL {

    GLuint CompileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        int success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512] = {};
            glGetShaderInfoLog(shader, sizeof(infoLog) - 1, nullptr, infoLog);
            std::cerr << "[GL] shader compilation error: " << infoLog << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint CompileProgram(const char* vertSrc, const char* fragSrc) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSrc);
        if (!vs) return 0;

        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
        if (!fs) {
            glDeleteShader(vs);
            return 0;
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        int success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512] = {};
            glGetProgramInfoLog(program, sizeof(infoLog) - 1, nullptr, infoLog);
            std::cerr << "[GL] program linking error: " << infoLog << std::endl;
            glDeleteProgram(program);
            program = 0;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);
        return program;
    }

}
