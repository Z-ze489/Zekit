#include "pch.h"

#include "ZeGLContext.h"
#include "ZeGLProgram.h"
#include <cctype>
#include <cstdio>
#include <iostream>

#ifndef ZEKIT_NO_GPU_EXPORT
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}
#endif

namespace {
    bool RequestDiscreteGPU() {
        const char* dllName =
#ifdef _WIN64
            "nvapi64.dll";
#else
            "nvapi.dll";
#endif
        HMODULE nvapi = LoadLibraryA(dllName);
        if (!nvapi) return false;

        using NvAPI_Initialize_t = int(*)(void);
        NvAPI_Initialize_t NvAPI_Initialize =
            reinterpret_cast<NvAPI_Initialize_t>(
                GetProcAddress(nvapi, "NvAPI_Initialize"));

        if (NvAPI_Initialize)
            NvAPI_Initialize();

        return true;
    }
}

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

#define WGL_DRAW_TO_WINDOW_ARB     0x2001
#define WGL_SUPPORT_OPENGL_ARB     0x2010
#define WGL_DOUBLE_BUFFER_ARB      0x2011
#define WGL_PIXEL_TYPE_ARB         0x2013
#define WGL_COLOR_BITS_ARB         0x2014
#define WGL_ALPHA_BITS_ARB         0x2021
#define WGL_DEPTH_BITS_ARB         0x2022
#define WGL_STENCIL_BITS_ARB       0x2023
#define WGL_TYPE_RGBA_ARB          0x202B

#define WGL_SAMPLE_BUFFERS_ARB     0x2041
#define WGL_SAMPLES_ARB            0x2042

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

static PFNWGLCREATECONTEXTATTRIBSARBPROC s_wglCreateContextAttribsARB = nullptr;
static PFNWGLCHOOSEPIXELFORMATARBPROC     s_wglChoosePixelFormatARB = nullptr;
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
static PFNWGLSWAPINTERVALEXTPROC          s_wglSwapIntervalEXT = nullptr;

namespace GL {
	bool GLContext::s_wglLoaded = false;
	bool GLContext::s_swapIntervalSupported = false;

	bool GLContext::LoadWGLExtensions() {
		if (s_wglLoaded) return true;

        const wchar_t TEMP_CLASS[] = L"ZekitTempWGLClass";
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = TEMP_CLASS;
        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        }

        HWND tempHwnd = CreateWindowExW(
            0, TEMP_CLASS, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 1, 1,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!tempHwnd) return false;

        HDC tempDC = GetDC(tempHwnd);

        PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;

        int fmt = ChoosePixelFormat(tempDC, &pfd);
        if (!fmt || !SetPixelFormat(tempDC, fmt, &pfd)) {
            ReleaseDC(tempHwnd, tempDC);
            DestroyWindow(tempHwnd);
            return false;
        }

        HGLRC tempRC = wglCreateContext(tempDC);
        if (!tempRC) {
            ReleaseDC(tempHwnd, tempDC);
            DestroyWindow(tempHwnd);
            return false;
        }
        wglMakeCurrent(tempDC, tempRC);

        s_wglCreateContextAttribsARB =
            (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
        s_wglChoosePixelFormatARB =
            (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        s_wglSwapIntervalEXT =
            (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        s_swapIntervalSupported = (s_wglSwapIntervalEXT != nullptr);

        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempRC);
        ReleaseDC(tempHwnd, tempDC);
        DestroyWindow(tempHwnd);

        s_wglLoaded = (s_wglCreateContextAttribsARB && s_wglChoosePixelFormatARB);
        return s_wglLoaded;
	}

    bool GLContext::Create(HWND hwnd, int major, int minor, bool coreProfile) {
        RequestDiscreteGPU();
        return CreateImpl(hwnd, nullptr, major, minor, coreProfile);
    }

    bool GLContext::CreateShared(HWND hwnd, const GLContext& share,
                                 int major, int minor, bool coreProfile) {
        if (!share.IsValid()) return false;
        return CreateImpl(hwnd, share.GetHGLRC(), major, minor, coreProfile);
    }

    bool GLContext::CreateImpl(HWND hwnd, HGLRC shareHGLRC,
                               int major, int minor, bool coreProfile) {
        if (m_hRC) Destroy();
        if (!hwnd) return false;
        if (!LoadWGLExtensions()) return false;

        m_hwnd = hwnd;
        m_hDC = GetDC(m_hwnd);
        if (!m_hDC) return false;

        const int pixelAttribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
            WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
            WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB,     32,
            WGL_ALPHA_BITS_ARB,     8,
            WGL_DEPTH_BITS_ARB,     24,
            WGL_STENCIL_BITS_ARB,   8,
            WGL_SAMPLE_BUFFERS_ARB, 1,
            WGL_SAMPLES_ARB,        4,
            0
        };
        int   pixelFormat = 0;
        UINT  numFormats = 0;
        if (!s_wglChoosePixelFormatARB(m_hDC, pixelAttribs, nullptr,
            1, &pixelFormat, &numFormats) || numFormats == 0){
            ReleaseDC(m_hwnd, m_hDC); m_hDC = nullptr;
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd) };
        DescribePixelFormat(m_hDC, pixelFormat, sizeof(pfd), &pfd);
        if (!SetPixelFormat(m_hDC, pixelFormat, &pfd)) {
            ReleaseDC(m_hwnd, m_hDC); m_hDC = nullptr;
            return false;
        }

        const int profileBit = coreProfile
            ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB
            : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;

        const int contextAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, major,
            WGL_CONTEXT_MINOR_VERSION_ARB, minor,
            WGL_CONTEXT_PROFILE_MASK_ARB,  profileBit,
            0
        };

        m_hRC = s_wglCreateContextAttribsARB(m_hDC, shareHGLRC, contextAttribs);
        if (!m_hRC) {
            ReleaseDC(m_hwnd, m_hDC); m_hDC = nullptr;
            return false;
        }

        MakeCurrent();

        if (!gladLoadGL()) {
            Destroy();
            return false;
        }

        return true;
    }

    GLuint GLContext::compileShader(GLenum type, const char* source) {
        return GL::CompileShader(type, source);
    }

    GLuint GLContext::createProgram(const char* vertSrc, const char* fragSrc) {
        return GL::CompileProgram(vertSrc, fragSrc);
    }

    void GLContext::MakeCurrent(){
        if (m_hDC && m_hRC)
            wglMakeCurrent(m_hDC, m_hRC);
    }

    void GLContext::DoneCurrent(){
        wglMakeCurrent(nullptr, nullptr);
    }

    void GLContext::SwapBuffers(){
        if (m_hDC)
            ::SwapBuffers(m_hDC);
    }

    void GLContext::SetSwapInterval(int interval) {
        if (!s_wglSwapIntervalEXT) return;

        if (m_swapApplied && interval == m_swapInterval) return;
        m_swapInterval = interval;
        m_swapApplied = true;
        s_wglSwapIntervalEXT(interval);
    }

    namespace {
        bool ContainsNoCase(const char* hay, const char* needle) {
            if (!hay || !needle) return false;
            for (const char* p = hay; *p; ++p) {
                const char* a = p;
                const char* b = needle;
                while (*a && *b &&
                    std::tolower(static_cast<unsigned char>(*a)) ==
                    std::tolower(static_cast<unsigned char>(*b))) {
                    ++a; ++b;
                }
                if (*b == '\0') return true;
            }
            return false;
        }
    }

    RendererInfo GLContext::GetRendererInfo() const {
        RendererInfo info;

        if (!m_hRC || wglGetCurrentContext() != m_hRC)
            return info;

        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

        info.vendor = vendor ? vendor : "";
        info.renderer = renderer ? renderer : "";
        info.version = version ? version : "";

        const bool vendorIsDgpu =
            ContainsNoCase(info.vendor, "NVIDIA") ||
            ContainsNoCase(info.vendor, "AMD") ||
            ContainsNoCase(info.vendor, "ATI");

        const bool looksIntegrated =
            ContainsNoCase(info.renderer, "Intel") ||
            ContainsNoCase(info.renderer, "UHD Graphics") ||
            ContainsNoCase(info.renderer, "HD Graphics") ||
            ContainsNoCase(info.renderer, "Iris") ||
            ContainsNoCase(info.renderer, "Microsoft Basic Render") ||
            ContainsNoCase(info.renderer, "llvmpipe");

        info.isDiscrete = vendorIsDgpu && !looksIntegrated;
        return info;
    }

    void GLContext::GetVersion(int& major, int& minor) const {
        major = minor = 0;
        if (!m_hRC || wglGetCurrentContext() != m_hRC) return;

        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (!v) return;

        int mj = 0, mn = 0;
        const char* p = v;
        while (*p >= '0' && *p <= '9') { mj = mj * 10 + (*p - '0'); ++p; }
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') { mn = mn * 10 + (*p - '0'); ++p; }
        }
        major = mj;
        minor = mn;
    }

    void GLContext::Destroy(){
        if (m_hRC) {
            if (wglGetCurrentContext() == m_hRC)
                wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_hRC);
            m_hRC = nullptr;
        }
        if (m_hDC && m_hwnd) {
            ReleaseDC(m_hwnd, m_hDC);
        }
        m_hDC = nullptr;
        m_hwnd = nullptr;
    }

    GLContext::~GLContext(){ Destroy(); }

    GLContext::GLContext(GLContext&& other) noexcept
        : m_hwnd(other.m_hwnd), m_hDC(other.m_hDC), m_hRC(other.m_hRC){
        other.m_hwnd = nullptr;
        other.m_hDC = nullptr;
        other.m_hRC = nullptr;
    }

    GLContext& GLContext::operator=(GLContext&& other) noexcept{
        if (this != &other) {
            Destroy();
            m_hwnd = other.m_hwnd;
            m_hDC = other.m_hDC;
            m_hRC = other.m_hRC;
            other.m_hwnd = nullptr;
            other.m_hDC = nullptr;
            other.m_hRC = nullptr;
        }
        return *this;
    }
}
