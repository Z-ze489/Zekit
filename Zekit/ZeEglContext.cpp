
#include "pch.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "ZeGLApi.h"

#if defined(__ANDROID__)
#  include <android/native_window.h>
#endif

#include "ZeEglContext.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <atomic>
#include <strings.h>
#include <string>
#include <utility>
#include <vector>

namespace {
    PFNEGLBINDAPIPROC             pEglBindAPI             = nullptr;
    PFNEGLCHOOSECONFIGPROC        pEglChooseConfig        = nullptr;
    PFNEGLCREATECONTEXTPROC       pEglCreateContext       = nullptr;
    PFNEGLCREATEWINDOWSURFACEPROC pEglCreateWindowSurface = nullptr;
    PFNEGLDESTROYCONTEXTPROC      pEglDestroyContext      = nullptr;
    PFNEGLDESTROYSURFACEPROC      pEglDestroySurface      = nullptr;
    PFNEGLGETDISPLAYPROC          pEglGetDisplay          = nullptr;
    PFNEGLGETERRORPROC            pEglGetError            = nullptr;
    PFNEGLGETPLATFORMDISPLAYPROC  pEglGetPlatformDisplay  = nullptr;
    PFNEGLGETPROCADDRESSPROC      pEglGetProcAddress      = nullptr;
    PFNEGLINITIALIZEPROC          pEglInitialize          = nullptr;
    PFNEGLMAKECURRENTPROC         pEglMakeCurrent         = nullptr;
    PFNEGLSWAPBUFFERSPROC         pEglSwapBuffers         = nullptr;
    PFNEGLSWAPINTERVALPROC        pEglSwapInterval        = nullptr;
    PFNEGLTERMINATEPROC           pEglTerminate           = nullptr;

    std::string g_eglError;

    bool LoadEgl() {
        if (pEglInitialize) return true;

        const char* lib =
#if defined(__ANDROID__) || defined(__OHOS__)
            "libEGL.so";
#else
            "libEGL.so.1";
#endif
        if (const char* v = std::getenv("ZEKIT_EGL_LIB"))
            if (*v) lib = v;

        void* h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            g_eglError = std::string("EGL library '") + lib +
                "' not found (install: sudo apt install libegl1). "
                "没有它 OpenGL 也建不起来 —— 两个后端都会失败。";
            return false;
        }

#define ZEKIT_EGL_SYM(slot, name)                                          \
        slot = reinterpret_cast<decltype(slot)>(dlsym(h, name));           \
        if (!slot) {                                                       \
            g_eglError = std::string("EGL library is missing entry '") +   \
                         name + "'";                                       \
            return false;                                                  \
        }
        ZEKIT_EGL_SYM(pEglBindAPI,             "eglBindAPI")
        ZEKIT_EGL_SYM(pEglChooseConfig,        "eglChooseConfig")
        ZEKIT_EGL_SYM(pEglCreateContext,       "eglCreateContext")
        ZEKIT_EGL_SYM(pEglCreateWindowSurface, "eglCreateWindowSurface")
        ZEKIT_EGL_SYM(pEglDestroyContext,      "eglDestroyContext")
        ZEKIT_EGL_SYM(pEglDestroySurface,      "eglDestroySurface")
        ZEKIT_EGL_SYM(pEglGetDisplay,          "eglGetDisplay")
        ZEKIT_EGL_SYM(pEglGetError,            "eglGetError")
        ZEKIT_EGL_SYM(pEglGetPlatformDisplay,  "eglGetPlatformDisplay")
        ZEKIT_EGL_SYM(pEglGetProcAddress,      "eglGetProcAddress")
        ZEKIT_EGL_SYM(pEglInitialize,          "eglInitialize")
        ZEKIT_EGL_SYM(pEglMakeCurrent,         "eglMakeCurrent")
        ZEKIT_EGL_SYM(pEglSwapBuffers,         "eglSwapBuffers")
        ZEKIT_EGL_SYM(pEglSwapInterval,        "eglSwapInterval")
        ZEKIT_EGL_SYM(pEglTerminate,           "eglTerminate")
#undef ZEKIT_EGL_SYM

        if (const char* v = std::getenv("ZEKIT_TRACE_LIFECYCLE"))
            if (*v && *v != '0')
                std::fprintf(stderr, "[Zekit/trace] EGL 已加载：%s\n", lib);
        return true;
    }
}

#define eglBindAPI             pEglBindAPI
#define eglChooseConfig        pEglChooseConfig
#define eglCreateContext       pEglCreateContext
#define eglCreateWindowSurface pEglCreateWindowSurface
#define eglDestroyContext      pEglDestroyContext
#define eglDestroySurface      pEglDestroySurface
#define eglGetDisplay          pEglGetDisplay
#define eglGetError            pEglGetError
#define eglGetPlatformDisplay  pEglGetPlatformDisplay
#define eglGetProcAddress      pEglGetProcAddress
#define eglInitialize          pEglInitialize
#define eglMakeCurrent         pEglMakeCurrent
#define eglSwapBuffers         pEglSwapBuffers
#define eglSwapInterval        pEglSwapInterval
#define eglTerminate           pEglTerminate

namespace GL {

    namespace {

        void* LoadGlProc(const char* name) {
            void* p = reinterpret_cast<void*>(eglGetProcAddress(name));
            if (p) return p;

            static void* libGL = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
            if (libGL) p = dlsym(libGL, name);
            return p;
        }

        enum class DeviceClass {
            Discrete,
            Integrated,
            Unknown,
            Software,
        };

        bool Contains(const char* haystack, const char* needle) {
            if (!haystack || !needle || !*needle) return false;
            const size_t n = std::strlen(needle);
            for (const char* p = haystack; *p; ++p) {
                if (::strncasecmp(p, needle, n) == 0) return true;
            }
            return false;
        }

        DeviceClass Classify(const char* s, bool isSoftware) {
            if (isSoftware) return DeviceClass::Software;
            if (Contains(s, "llvmpipe") || Contains(s, "softpipe") ||
                Contains(s, "swrast")   || Contains(s, "swiftshader")) {
                return DeviceClass::Software;
            }
            if (Contains(s, "nvidia") || Contains(s, "amd") ||
                Contains(s, "ati technologies") || Contains(s, "advanced micro devices") ||
                Contains(s, "radeon")) {
                return DeviceClass::Discrete;
            }
            if (Contains(s, "intel")) return DeviceClass::Integrated;
            return DeviceClass::Unknown;
        }

        int Rank(DeviceClass cls, Rhi::Gpu gpu) {
            static const DeviceClass kDiscreteFirst[]   = {
                DeviceClass::Discrete, DeviceClass::Integrated,
                DeviceClass::Unknown,  DeviceClass::Software,
            };
            static const DeviceClass kIntegratedFirst[] = {
                DeviceClass::Integrated, DeviceClass::Discrete,
                DeviceClass::Unknown,    DeviceClass::Software,
            };

            const DeviceClass* order = kDiscreteFirst;
            size_t count = sizeof(kDiscreteFirst) / sizeof(kDiscreteFirst[0]);

            if (gpu == Rhi::Gpu::Integrated) {
                order = kIntegratedFirst;
                count = sizeof(kIntegratedFirst) / sizeof(kIntegratedFirst[0]);
            } else if (gpu == Rhi::Gpu::Discrete) {
                return cls == DeviceClass::Discrete ? 0 : INT_MAX;
            }

            for (size_t i = 0; i < count; ++i) {
                if (order[i] == cls) return static_cast<int>(i);
            }
            return INT_MAX;
        }

        template <size_t N>
        void CopyStr(char (&dst)[N], const char* src) {
            std::snprintf(dst, N, "%s", src ? src : "");
        }

        void ApplyPrimeOffloadEnv(Rhi::Gpu gpu) {
            if (gpu == Rhi::Gpu::Integrated) {
                unsetenv("__NV_PRIME_RENDER_OFFLOAD");
                return;
            }
            if (!std::getenv("__NV_PRIME_RENDER_OFFLOAD")) {
                setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
            }
        }

        const EGLint kConfigAttrs[] = {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };

    }

#if defined(__ANDROID__) || defined(__OHOS__)
    bool EglContext::CreateGlesOnNativeWindow(void* nativeWindow,
                                              const char* platformTag,
                                              bool setBuffersGeometry) {
        if (IsValid()) return true;
        if (!nativeWindow) {
            std::cerr << "[EGL] " << platformTag << ": 窗口为空\n";
            return false;
        }

        if (!LoadEgl()) {
            std::cerr << "[EGL] " << g_eglError << "\n";
            return false;
        }
        m_deviceInfo = {};

        auto Fail = [this, platformTag](const char* what) {
            std::cerr << "[EGL] " << platformTag << ": " << what << " (EGL error 0x"
                << std::hex << eglGetError() << std::dec << ")\n";
            Destroy();
            return false;
        };

        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) return Fail("eglGetDisplay(EGL_DEFAULT_DISPLAY) 失败");

        EGLint mj = 0, mn = 0;
        if (!eglInitialize(dpy, &mj, &mn)) return Fail("eglInitialize 失败");
        if (!eglBindAPI(EGL_OPENGL_ES_API)) return Fail("eglBindAPI(EGL_OPENGL_ES_API) 失败");

        const EGLint cfgAttrs[] = {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE,     8,
            EGL_GREEN_SIZE,   8,
            EGL_BLUE_SIZE,    8,
            EGL_ALPHA_SIZE,   8,
            EGL_DEPTH_SIZE,   0,
            EGL_STENCIL_SIZE, 0,
            EGL_NONE
        };
        EGLConfig cfg = nullptr;
        EGLint    nc  = 0;
        if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &nc) || nc == 0)
            return Fail("eglChooseConfig 找不到 GLES3 窗口 config");

        const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
        if (ctx == EGL_NO_CONTEXT) return Fail("eglCreateContext(GLES3) 失败");

        if (setBuffersGeometry) {
#if defined(__ANDROID__)
            EGLint nativeFmt = 0;
            eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &nativeFmt);
            ANativeWindow_setBuffersGeometry(
                static_cast<ANativeWindow*>(nativeWindow), 0, 0, nativeFmt);
#endif
        }

        EGLSurface surf = eglCreateWindowSurface(dpy, cfg,
            (EGLNativeWindowType)nativeWindow, nullptr);
        if (surf == EGL_NO_SURFACE) return Fail("eglCreateWindowSurface 失败");
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) return Fail("eglMakeCurrent 失败");

        m_dpy    = dpy;
        m_surf   = surf;
        m_ctx    = ctx;
        m_config = cfg;

        while (glGetError() != GL_NO_ERROR) {}

        const auto str = [](GLenum e) -> const char* {
            const char* s = reinterpret_cast<const char*>(glGetString(e));
            return s ? s : "";
        };
        std::snprintf(m_deviceInfo.name,   sizeof(m_deviceInfo.name),   "%s", str(GL_RENDERER));
        std::snprintf(m_deviceInfo.driver, sizeof(m_deviceInfo.driver), "%s", str(GL_VERSION));
        std::snprintf(m_deviceInfo.api,    sizeof(m_deviceInfo.api),    "OpenGL ES");
        m_deviceInfo.isDiscrete = false;
        m_deviceInfo.valid = true;

        std::cerr << "[EGL] " << platformTag << ": GLES" << mj << "." << mn
                  << "  renderer=" << str(GL_RENDERER)
                  << "  version=" << str(GL_VERSION) << "\n";
        return true;
    }

#if defined(__ANDROID__)
    bool EglContext::CreateAndroid(void* nativeWindow, Rhi::Gpu gpu) {
        (void)gpu;
        return CreateGlesOnNativeWindow(nativeWindow, "CreateAndroid",
                                        true);
    }
#endif

#if defined(__OHOS__)
    bool EglContext::CreateOHOS(void* nativeWindow, Rhi::Gpu gpu) {
        (void)gpu;
        return CreateGlesOnNativeWindow(nativeWindow, "CreateOHOS",
                                        false);
    }
#endif

#endif

    EglContext::~EglContext() {
        Destroy();
    }

#if !defined(__ANDROID__) && !defined(__OHOS__)
    bool EglContext::Create(void* x11Display, unsigned long long xid,
                            int major, int minor, bool coreProfile,
                            Rhi::Gpu gpu, unsigned long nativeVisualId) {
        if (IsValid()) return true;
        if (!x11Display || xid == 0) {
            std::cerr << "[EGL] Create: missing X11 display or window id\n";
            return false;
        }

        if (!LoadEgl()) {
            std::cerr << "[EGL] " << g_eglError << "\n";
            return false;
        }

        m_deviceInfo = {};

        ApplyPrimeOffloadEnv(gpu);

        auto Fail = [this](const char* what) {
            std::cerr << "[EGL] " << what << " (EGL error 0x"
                << std::hex << eglGetError() << std::dec << ")\n";
            Destroy();
            return false;
        };

        auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
            eglGetProcAddress("eglQueryDevicesEXT"));
        auto deviceString = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
            eglGetProcAddress("eglQueryDeviceStringEXT"));

        EGLConfig cfg = nullptr;
        auto prepare = [&](EGLDisplay d) -> bool {
            EGLint mj = 0, mn = 0;
            if (!eglInitialize(d, &mj, &mn)) return false;
            eglBindAPI(EGL_OPENGL_API);
            EGLConfig c = nullptr;
            EGLint nc = 0;
            EGLConfig cands[64] = {};
            if (!eglChooseConfig(d, kConfigAttrs, cands, 64, &nc) || nc == 0) return false;

            c = cands[0];
            if (nativeVisualId != 0) {
                bool matched = false;
                for (EGLint i = 0; i < nc; ++i) {
                    EGLint vid = 0;
                    if (eglGetConfigAttrib(d, cands[i], EGL_NATIVE_VISUAL_ID, &vid)
                        && static_cast<unsigned long>(vid) == nativeVisualId) {
                        c = cands[i];
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    std::cerr << "[EGL] 没有与窗口 visual(0x" << std::hex << nativeVisualId
                        << std::dec << ") 匹配的 EGLConfig —— 这个窗口是 32 位 ARGB 的"
                           "（逐像素透明浮层），本机驱动不支持给它建 GL 表面。\n";
                    return false;
                }
            }
            cfg = c;
            return true;
        };

        EGLDisplay dpy = EGL_NO_DISPLAY;
        bool pickedByDevice = false;

        if (queryDevices && deviceString) {
            EGLint n = 0;
            if (queryDevices(0, nullptr, &n) && n > 0) {
                std::vector<EGLDeviceEXT> devs(static_cast<size_t>(n));
                if (queryDevices(n, devs.data(), &n)) {
                    std::vector<std::pair<int, EGLDeviceEXT>> cand;
                    cand.reserve(static_cast<size_t>(n));
                    for (EGLint i = 0; i < n; ++i) {
                        const char* vendor = deviceString(devs[i], EGL_VENDOR);
                        const char* exts   = deviceString(devs[i], EGL_EXTENSIONS);
                        const bool soft = Contains(exts, "EGL_MESA_device_software");
                        const int r = Rank(Classify(vendor, soft), gpu);
                        if (r != INT_MAX) cand.emplace_back(r, devs[i]);
                    }
                    std::sort(cand.begin(), cand.end(),
                        [](const std::pair<int, EGLDeviceEXT>& a,
                           const std::pair<int, EGLDeviceEXT>& b) {
                            return a.first < b.first;
                        });

                    for (const auto& c : cand) {
                        EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                                                             c.second, nullptr);
                        if (d == EGL_NO_DISPLAY) continue;
                        if (prepare(d)) {
                            dpy = d;
                            pickedByDevice = true;
                            break;
                        }
                        eglTerminate(d);
                    }
                }
            }
        }

        if (!pickedByDevice) {
            dpy = eglGetPlatformDisplay(EGL_PLATFORM_X11_EXT, x11Display, nullptr);
            if (dpy == EGL_NO_DISPLAY)
                dpy = eglGetDisplay(static_cast<EGLNativeDisplayType>(x11Display));
            if (dpy == EGL_NO_DISPLAY) return Fail("no usable EGL display");
            if (!prepare(dpy)) return Fail("no EGL config for window surface + OpenGL");

            if (gpu == Rhi::Gpu::Discrete) {
                std::cerr << "[EGL] no GPU can present to an X11 window through "
                             "EGL_PLATFORM_DEVICE_EXT on this driver; trying the "
                             "platform-default display instead.\n";
            }
        }

        m_dpy = dpy;

        EGLSurface surf = eglCreateWindowSurface(
            dpy, cfg, static_cast<EGLNativeWindowType>(xid), nullptr);
        if (surf == EGL_NO_SURFACE)
            return Fail("eglCreateWindowSurface failed");

        EGLContext ctx = EGL_NO_CONTEXT;
        if (coreProfile) {
            const EGLint attrs[] = {
                EGL_CONTEXT_MAJOR_VERSION, major,
                EGL_CONTEXT_MINOR_VERSION, minor,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };
            ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attrs);
        }
        if (ctx == EGL_NO_CONTEXT && coreProfile) {
            const EGLint attrs[] = {
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_CONTEXT_MINOR_VERSION, 3,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };
            ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attrs);
        }
        if (ctx == EGL_NO_CONTEXT) {
            const EGLint attrs[] = { EGL_NONE };
            ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attrs);
        }
        if (ctx == EGL_NO_CONTEXT)
            return Fail("eglCreateContext failed");

        m_surf   = surf;
        m_ctx    = ctx;
        m_config = cfg;

        if (!eglMakeCurrent(dpy, surf, surf, ctx))
            return Fail("eglMakeCurrent failed");

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(&LoadGlProc)))
            return Fail("gladLoadGLLoader failed");

        const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));

        CopyStr(m_deviceInfo.name,   renderer ? renderer : "(unknown renderer)");
        CopyStr(m_deviceInfo.driver, version  ? version  : "(unknown version)");
        CopyStr(m_deviceInfo.api,    "OpenGL");
        m_deviceInfo.isDiscrete =
            Classify(vendor, false) == DeviceClass::Discrete ||
            Classify(renderer, false) == DeviceClass::Discrete;
        m_deviceInfo.valid = true;

        if (gpu == Rhi::Gpu::Discrete && !m_deviceInfo.isDiscrete) {
            std::cerr << "[EGL] Gpu::Discrete requested but the actual GL renderer is '"
                << m_deviceInfo.name << "' (not a discrete GPU).\n"
                   "      本类已经在创建前设过 __NV_PRIME_RENDER_OFFLOAD=1 了，也就是说\n"
                   "      PRIME 卸载这条路没能把独显带出来。常见原因：\n"
                   "        · 这台机器没有 NVIDIA 独显，或驱动没装好（nvidia-smi 看一下）\n"
                   "        · X 屏不是 NVIDIA 驱动的 —— 混合本上需要先配好 PRIME 卸载\n"
                   "        · 用 Rhi::Api::Vulkan 代替：那里的 per-GPU 选择由库内精确完成，\n"
                   "          不依赖任何环境变量或驱动配置（那也是本库的默认后端）\n";
            Destroy();
            return false;
        }

        return true;
    }

#endif

    bool EglContext::CreateShared(void* x11Display, unsigned long long xid,
                                  const EglContext& share,
                                  int major, int minor, bool coreProfile) {
        if (IsValid()) return true;
        if (!share.IsValid()) return false;
        if (!x11Display || xid == 0) return false;
        if (!share.m_dpy || !share.m_config) return false;

        if (!LoadEgl()) {
            std::cerr << "[EGL] " << g_eglError << "\n";
            return false;
        }
        m_deviceInfo = {};

        auto Fail = [this](const char* what) {
            std::cerr << "[EGL] " << what << " (EGL error 0x"
                << std::hex << eglGetError() << std::dec << ")\n";
            Destroy();
            return false;
        };

        m_dpy    = share.m_dpy;
        m_config = share.m_config;
        m_ownsDisplay = false;

        m_surf = eglCreateWindowSurface(
            static_cast<EGLDisplay>(m_dpy), static_cast<EGLConfig>(m_config),
            reinterpret_cast<EGLNativeWindowType>(static_cast<uintptr_t>(xid)),
            nullptr);
        if (static_cast<EGLSurface>(m_surf) == EGL_NO_SURFACE)
            return Fail("eglCreateWindowSurface(child) failed");

        auto MakeCtx = [&](int mj, int mn, bool core) -> EGLContext {
            if (!core) {
                const EGLint plain[] = { EGL_NONE };
                return eglCreateContext(static_cast<EGLDisplay>(m_dpy),
                    static_cast<EGLConfig>(m_config),
                    static_cast<EGLContext>(share.m_ctx), plain);
            }
            const EGLint attrs[] = {
                EGL_CONTEXT_MAJOR_VERSION, mj,
                EGL_CONTEXT_MINOR_VERSION, mn,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };
            return eglCreateContext(static_cast<EGLDisplay>(m_dpy),
                static_cast<EGLConfig>(m_config),
                static_cast<EGLContext>(share.m_ctx), attrs);
        };

        m_ctx = MakeCtx(major, minor, coreProfile);
        if (!m_ctx && coreProfile) m_ctx = MakeCtx(3, 3, true);
        if (!m_ctx)                m_ctx = MakeCtx(0, 0, false);
        if (static_cast<EGLContext>(m_ctx) == EGL_NO_CONTEXT)
            return Fail("eglCreateContext(shared) failed");

        if (!eglMakeCurrent(static_cast<EGLDisplay>(m_dpy),
                            static_cast<EGLSurface>(m_surf),
                            static_cast<EGLSurface>(m_surf),
                            static_cast<EGLContext>(m_ctx)))
            return Fail("eglMakeCurrent(shared) failed");

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(&LoadGlProc)))
            return Fail("gladLoadGLLoader(shared) failed");

        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        CopyStr(m_deviceInfo.name,   renderer ? renderer : "(unknown renderer)");
        CopyStr(m_deviceInfo.driver, version  ? version  : "(unknown version)");
        CopyStr(m_deviceInfo.api,    "OpenGL");

        return true;
    }

    void EglContext::Destroy() {
        EGLDisplay dpy = static_cast<EGLDisplay>(m_dpy);
        if (dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_surf)   eglDestroySurface(dpy, static_cast<EGLSurface>(m_surf));
            if (m_ctx)    eglDestroyContext(dpy, static_cast<EGLContext>(m_ctx));
            if (m_ownsDisplay) eglTerminate(dpy);
        }
        m_surf   = nullptr;
        m_ctx    = nullptr;
        m_config = nullptr;
        m_dpy    = nullptr;
        m_deviceInfo = {};
    }

    void EglContext::MakeCurrent() {
        if (!m_dpy) return;
        eglMakeCurrent(static_cast<EGLDisplay>(m_dpy),
            static_cast<EGLSurface>(m_surf),
            static_cast<EGLSurface>(m_surf),
            static_cast<EGLContext>(m_ctx));
    }

    void EglContext::DoneCurrent() {
        if (!m_dpy) return;
        eglMakeCurrent(static_cast<EGLDisplay>(m_dpy),
            EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    void EglContext::SwapBuffers() {
        if (!m_dpy || !m_surf) return;
        const EGLBoolean ok = eglSwapBuffers(static_cast<EGLDisplay>(m_dpy),
                                             static_cast<EGLSurface>(m_surf));
        if (ok != EGL_TRUE) {
            static std::atomic<int> logged{ 0 };
            if (logged.fetch_add(1) < 5)
                std::cerr << "[Zekit/EGL] eglSwapBuffers 失败 err=0x"
                    << std::hex << pEglGetError() << std::dec
                    << "（surf=" << m_surf << "）\n";
        }
    }

    void EglContext::SetSwapInterval(int interval) {
        if (!m_dpy || !m_surf) return;
        if (interval == m_swapInterval) return;
        m_swapInterval = interval;
        eglSwapInterval(static_cast<EGLDisplay>(m_dpy), interval);
    }

}
