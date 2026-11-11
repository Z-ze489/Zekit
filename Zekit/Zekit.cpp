#include "pch.h"
#include "framework.h"

#include "include/Zekit.h"
#include "ZePlatformWindow.h"
#include "ZeRhiBackend.h"
#include "ZeExternalBackend.h"
#include "include/ZeCanvasRenderer.h"
#include "ZeHitTest.h"
#include "ZeFrameCollector.h"
#include "ZePaintCache.h"
#include "ZeFontRenderer.h"
#include "ZeImageStore.h"
#include "include/ZeLayoutBase.h"
#include "include/ZeTooltip.h"
#include "include/ZeCanvasView.h"
#include "ZeOverlayHost.h"
#include "ZeSubWindow.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <typeinfo>
#include <iostream>
#include <limits>
#include <mutex>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

#define ZEKIT_VERSION "1.0.0"

#if defined(_WIN32)
#  define ZEKIT_HAS_COMPOSITION_API 1
#else
#  define ZEKIT_HAS_COMPOSITION_API 0
#endif

namespace {
    int& LiveWindowCount() { static int n = 0; return n; }
    int& AnchorWindowCount() { static int n = 0; return n; }
    int& ExecLoopCount()   { static int n = 0; return n; }

    inline bool EnvFlag(const char* name) {
        const char* v = std::getenv(name);
        return v && *v != '\0' && *v != '0';
    }

    inline bool TraceLoopVerbose() {
        static const bool on = EnvFlag("ZEKIT_TRACE_LOOP");
        return on;
    }

    inline bool PaintCacheOff() {
        static const bool off = EnvFlag("ZEKIT_NO_PAINT_CACHE");
        return off;
    }

    inline bool TracePaint() {
        static const bool on = EnvFlag("ZEKIT_TRACE_PAINT");
        return on;
    }

    std::string ThemeHex(const Kit::Color4f& c) {
        const auto ch = [](float v) {
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            return static_cast<unsigned>(v * 255.0f + 0.5f);
            };
        char buf[8];
        std::snprintf(buf, sizeof buf, "#%06X", (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b));
        return buf;
    }

    constexpr std::size_t kKeySlots = 256;

    constexpr int kAutoGeometry = (std::numeric_limits<int>::min)();

    constexpr int kAutoSizeNum = 3;
    constexpr int kAutoSizeDen = 5;
    constexpr int kAutoMinW = 320;
    constexpr int kAutoMinH = 240;
    constexpr int kAutoFallbackW = 480;
    constexpr int kAutoFallbackH = 320;

    int AutoLength(int hostLen, int floorLen, int fallbackLen) {
        if (hostLen <= 0) return fallbackLen;
        int v = hostLen * kAutoSizeNum / kAutoSizeDen;
        if (v < floorLen) v = floorLen;
        if (v > hostLen) v = hostLen;
        return std::max(1, v);
    }

    void ResolveChildGeometry(Kit::Window& host, int& x, int& y, int& w, int& h) {
        const bool autoW = (w <= 0);
        const bool autoH = (h <= 0);
        const bool autoX = (x == kAutoGeometry);
        const bool autoY = (y == kAutoGeometry);
        if (!autoW && !autoH && !autoX && !autoY) return;

        int hostW = 0, hostH = 0;
        host.GetClientSize(hostW, hostH);

        if (autoW) w = AutoLength(hostW, kAutoMinW, kAutoFallbackW);
        if (autoH) h = AutoLength(hostH, kAutoMinH, kAutoFallbackH);

        if (autoX) x = (hostW > 0) ? std::max(0, (hostW - w) / 2) : 0;
        if (autoY) y = (hostH > 0) ? std::max(0, (hostH - h) / 2) : 0;
    }
}

extern "C" const char* zekit_version() {
    return "Zekit version: " ZEKIT_VERSION;
}

namespace Kit {

    std::atomic<int>& FrameDepth() {
        static std::atomic<int> n{ 0 };
        return n;
    }

    bool AnyFrameActive() {
        return FrameDepth().load(std::memory_order_acquire) > 0;
    }

    struct FrameDepthGuard {
        FrameDepthGuard() { FrameDepth().fetch_add(1, std::memory_order_acq_rel); }
        ~FrameDepthGuard() { FrameDepth().fetch_sub(1, std::memory_order_acq_rel); }
        FrameDepthGuard(const FrameDepthGuard&) = delete;
        FrameDepthGuard& operator=(const FrameDepthGuard&) = delete;
    };

    void AttachPlatformHost(void* host) {
        Plat::AttachPlatformHost(host);
    }

    bool HideConsole() {
        return Plat::HideConsole();
    }

    bool ShowConsole() {
        return Plat::ShowConsole();
    }

    void SetupEntryConsole() {
        Plat::SetupEntryConsole();
    }

    struct Window::Impl : Plat::IWindowHost {
        std::unique_ptr<Plat::IWindow> m_native;

        void* NativeHandle() const { return m_native ? m_native->NativeHandle() : nullptr; }

        bool NativeClosed() const { return !m_native || m_native->IsClosed(); }

        void* m_nativeHandle = nullptr;

        bool  m_isChild = false;

        bool  m_gadget = false;

        Impl* m_parentImpl = nullptr;

        bool m_inheritOnParentClose = false;
        bool m_inherited = false;
        bool m_countsAsLive = false;
        bool m_countsAsAnchor = false;
        std::function<void(Window&)> m_onInherited;
        std::vector<Impl*> m_dependents;

        bool m_backendShared = false;

        SubWindow* m_emulated = nullptr;

        bool m_emulatedClosed = false;

        bool m_childDisplayPrimed = false;

        bool m_userHidden = false;

        void createEmulatedChild(Window& parent, int x, int y, int width, int height) {
            SubWindowDesc d;
            d.x      = static_cast<float>(x);
            d.y      = static_cast<float>(y);
            d.width  = static_cast<float>(width);
            d.height = static_cast<float>(height);
            d.closable = true;
            d.movable  = true;

            SubWindow& sw = parent.AddWidget(SubWindow(d));
            sw.SetClosable(true);
            sw.SetMovable(true);
            m_emulated = &sw;

            sw.Open();

            sw.SetOnClosed([this](Widget&) {
                m_emulatedClosed = true;
                if (m_onClosed && m_ownerWindow) {
                    std::function<void(Window&)> cb = m_onClosed;
                    cb(*m_ownerWindow);
                }
            });

            m_followSystem = false;
            m_dark = parent.m_impl->m_dark;
            m_userTheme = false;
            m_theme = parent.m_impl->m_theme;
            m_api  = parent.m_impl->m_api;
            m_gpu  = parent.m_impl->m_gpu;

        }

        void registerDependent(Impl* child) {
            if (!child) return;
            m_dependents.push_back(child);
        }

        void unregisterDependent(Impl* child) {
            m_dependents.erase(
                std::remove(m_dependents.begin(), m_dependents.end(), child),
                m_dependents.end());
        }

        static std::vector<Impl*>& HeirRegistry() { static std::vector<Impl*> v; return v; }

        static void RegisterHeir(Impl* p) {
            if (!p) return;
            auto& v = HeirRegistry();
            if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
        }

        static void UnregisterHeir(Impl* p) {
            auto& v = HeirRegistry();
            v.erase(std::remove(v.begin(), v.end(), p), v.end());
        }

        static bool AnyHeirWantsFrame() {
            for (Impl* h : HeirRegistry())
                if (h && !h->NativeClosed() && h->anyWidgetWantsFrame()) return true;
            return false;
        }

        static void RenderHeirFrames(bool force) {
            std::vector<Impl*> heirs = HeirRegistry();
            for (Impl* h : heirs) {
                if (!h || h->NativeClosed()) continue;
                if (!force && !h->anyWidgetWantsFrame()) continue;
                h->RenderFrame();
            }
        }

        static std::vector<Impl*>& ExecHosts() { static std::vector<Impl*> v; return v; }

        bool hasSharedDependents() const {
            for (Impl* kid : m_dependents) {
                if (!kid) continue;
                if (kid->NativeClosed()) continue;
                if (kid->m_backendShared) return true;
            }
            return false;
        }

        void promoteInheritingChildren() {
            if (m_dependents.empty()) return;

            std::vector<Impl*> kids = m_dependents;
            for (Impl* kid : kids) {
                if (!kid || kid->m_inherited) continue;
                if (!kid->m_inheritOnParentClose) continue;
                if (kid->NativeClosed()) continue;
                if (!kid->m_native) continue;
                if (!kid->m_native->ReleaseFromHost()) continue;

                kid->m_parentImpl->unregisterDependent(kid);
                kid->m_parentImpl = nullptr;
                kid->m_isChild = false;
                kid->m_inherited = true;
                if (!kid->m_countsAsLive) {
                    kid->m_countsAsLive = true;
                    ++LiveWindowCount();
                }
                if (!kid->m_countsAsAnchor) {
                    kid->m_countsAsAnchor = true;
                    ++AnchorWindowCount();
                }
                RegisterHeir(kid);

                if (kid->m_onInherited && kid->m_ownerWindow) {
                    std::function<void(Window&)> cb = kid->m_onInherited;
                    cb(*kid->m_ownerWindow);
                }
            }
        }

        void dismissOwnedSurfaces() {
            if (!m_ownerWindow) return;

            OverlayHost::Dismiss(*m_ownerWindow);
            NotifyToastHostClosing(*m_ownerWindow);

            std::vector<Impl*> kids = m_dependents;
            for (Impl* kid : kids) {
                if (!kid || kid->NativeClosed()) continue;
                if (!kid->m_ownerWindow) continue;
                kid->m_ownerWindow->Close();
            }
        }

        std::unique_ptr<Rhi::IBackend> m_renderBackend;

        Rhi::Api m_api = Rhi::DefaultApi();

        Rhi::Gpu m_gpu = Rhi::DefaultGpu();

        float m_clearAlpha = 1.0f;

        std::shared_ptr<Kit::ICanvasRenderer> m_externalRenderer;

        bool m_geomValid = false;
        int  m_geomOx = 0;
        int  m_geomOy = 0;
        int  m_geomW = 0;
        int  m_geomH = 0;

        bool createBackendObject() {
            if (m_renderBackend) return true;

            if (m_externalRenderer) {
                m_renderBackend = std::make_unique<Rhi::ExternalBackend>(m_externalRenderer);
                m_api = Rhi::Api::External;
                return true;
            }

            if (m_isChild && m_parentImpl) {
                if (!m_parentImpl->m_renderBackend || !m_parentImpl->m_renderBackend->IsReady())
                    m_parentImpl->ensureBackend();

                if (m_parentImpl->m_renderBackend
                    && m_parentImpl->m_renderBackend->CanShareContext()) {
                    int W = 0, H = 0;
                    if (m_native) m_native->GetClientSize(W, H);
                    m_renderBackend.reset(
                        m_parentImpl->m_renderBackend->CreateShared(m_nativeHandle, W, H,
                                                                    m_clearAlpha));
                    if (m_renderBackend) {
                        m_renderBackend->SetClearAlpha(m_clearAlpha);
                        m_backendShared = true;
                        return true;
                    }
                }
            }

            m_renderBackend.reset(Rhi::CreateBackend(m_api));
            if (!m_renderBackend) return false;
            m_backendShared = false;

            m_renderBackend->SetGpu(m_gpu);
            m_renderBackend->SetClearAlpha(m_clearAlpha);
            return true;
        }

        bool preheatBackend() {
            if (const char* v = std::getenv("ZEKIT_PREHEAT_DEVICE"))
                if (*v == '0' || *v == '\0') return false;

            if (!m_native || m_native->IsClosed()) return false;
            if (!m_nativeHandle) return false;
            if (!createBackendObject()) return false;
            return m_renderBackend->Preheat(m_nativeHandle);
        }

        bool ensureBackend() {
            if (m_renderBackend && m_renderBackend->IsReady()) return true;
            if (!m_renderEnabled) return false;
            if (!m_native || m_native->IsClosed()) return false;

            if (m_nativeHandle == nullptr) return false;

            int W = 0, H = 0;
            m_native->GetClientSize(W, H);

            if (!createBackendObject()) return false;

            if (!m_renderBackend->Init(m_nativeHandle, W, H)) {
                if (m_renderBackend->api() == Rhi::Api::External) return false;

                const Rhi::Api requested = m_renderBackend->api();

                if (requested == Rhi::Api::Software) {
                    m_renderBackend.reset();
                    return false;
                }

                Rhi::Api fallback[2];
                int n = 0;
                if (requested != Rhi::Api::OpenGL) fallback[n++] = Rhi::Api::OpenGL;
                fallback[n++] = Rhi::Api::Software;

                Rhi::Api landed = requested;
                bool ok = false;
                for (int i = 0; i < n && !ok; ++i) {
                    const Rhi::Api next = fallback[i];
                    m_renderBackend.reset(Rhi::CreateBackend(next));
                    if (!m_renderBackend) continue;
                    m_renderBackend->SetGpu(m_gpu);
                    m_renderBackend->SetClearAlpha(m_clearAlpha);
                    if (m_renderBackend->Init(m_nativeHandle, W, H)) {
                        ok = true;
                        landed = next;
                    } else {
                        m_renderBackend.reset();
                    }
                }
                if (!ok) return false;

                std::cerr << "[Zekit] " << Rhi::ApiName(requested)
                          << " init failed, falling back to " << Rhi::ApiName(landed)
                          << "." << std::endl;
            }
            else {
            }

            m_renderBackend->AttachTextureSinks(&m_fontRenderer, &m_imageStore);

            if (m_clearAlpha <= 0.0f && m_native && !m_native->IsClosed()) {
                const bool perPixel = m_renderBackend->IsPerPixelTransparent();
                m_native->SetPerPixelTransparent(perPixel);
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] 逐像素透明核对：后端报 "
                        << (perPixel ? "true（背景会透）" : "false（合成链路不支持 alpha，退成不透明）")
                        << "\n";
            }

            return true;
        }

        FontRenderer  m_fontRenderer;
        ImageStore    m_imageStore;
        Theme         m_theme = Themes::Light();

        bool m_dark = false;
        bool m_followSystem = true;
        bool m_userTheme = false;
        Theme m_baseTheme = Themes::Light();

        std::vector<Widget*> m_widgets;

        bool m_sawNativeSurface = false;

        std::vector<std::unique_ptr<Widget>> m_owned;

        std::vector<std::unique_ptr<Window>> m_childWindows;

        FrameCollector       m_collector;

        FrameCollector       m_recorder;
        std::atomic<Widget*> m_paintingWidget{ nullptr };
        uint64_t             m_themeEpoch = 1;
        std::atomic<bool>    m_allPaintDirty{ false };
        bool                 m_frameAllDirty = false;

        std::atomic<uint32_t> m_frameRequests{ 0 };
        uint32_t              m_frameRequestLogs = 0;

        int  m_lastPaintW = -1;
        int  m_lastPaintH = -1;
        std::chrono::steady_clock::time_point m_lastPaintAt{};

        std::chrono::steady_clock::time_point m_lastSizeChangeAt{};

        bool m_geometrySync = false;

        int m_framePhase = 0;

        void markAllPaintDirty() {
            std::vector<Widget*> stack(m_widgets.begin(), m_widgets.end());
            while (!stack.empty()) {
                Widget* w = stack.back();
                stack.pop_back();
                if (!w) continue;
                PaintCacheAccess::MarkDirty(*w);
                for (Widget* c : w->children()) if (c) stack.push_back(c);
            }
        }

        void verifyFragments() {
            static const bool enabled = [] {
                const char* v = std::getenv("ZEKIT_VERIFY_PAINT");
                return v && *v && *v != '0';
                }();
            if (!enabled) return;

            std::size_t checked = 0, bad = 0;
            std::vector<Widget*> stack(m_widgets.begin(), m_widgets.end());
            while (!stack.empty()) {
                Widget* w = stack.back();
                stack.pop_back();
                if (!w) continue;
                for (Widget* c : w->children()) if (c) stack.push_back(c);

                if (!w->isVisible() || w->paintsOnTop()) continue;
                if (w->wantsAnimationFrame()) continue;
                PaintFragment* pf = PaintCacheAccess::Get(*w);
                if (!pf || !pf->valid || pf->timed) continue;

                FrameCollector::Fragment fresh;
                const uint64_t ce0 = m_fontRenderer.ContentEpoch();
                const uint64_t miss0 = m_fontRenderer.MissCount();

                m_recorder.BeginCapture(fresh);
                if (pf->hasClip && pf->clipW > 0.0f && pf->clipH > 0.0f)
                    m_recorder.PushClip(pf->clipX, pf->clipY, pf->clipW, pf->clipH);
                m_paintingWidget = w;
                w->Paint(m_recorder);
                m_paintingWidget = nullptr;
                m_recorder.EndCapture(fresh);

                if (m_fontRenderer.ContentEpoch() != ce0 || m_fontRenderer.MissCount() != miss0)
                    continue;
                if (pf->env.glyphContentEpoch != m_fontRenderer.ContentEpoch())
                    continue;

                ++checked;
                if (!SameFragment(fresh, pf->frag)) {
                    ++bad;
                    if (bad <= 8) {
                        const std::size_t n = std::min(fresh.verts.size(), pf->frag.verts.size());
                        std::size_t firstDiff = n;
                        for (std::size_t i = 0; i < n; ++i) {
                            if (std::memcmp(&fresh.verts[i], &pf->frag.verts[i], sizeof(UIVertex)) != 0) {
                                firstDiff = i; break;
                            }
                        }
                        std::cerr << "[Zekit/verify] 片段与重录不一致：" << typeid(*w).name()
                            << "（" << w->width() << "x" << w->height()
                            << " @ " << w->x() << "," << w->y()
                            << "）录 " << pf->frag.verts.size() << " 顶点 / "
                            << pf->frag.idx.size() << " 索引，现 "
                            << fresh.verts.size() << " 顶点 / "
                            << fresh.idx.size() << " 索引";
                        if (firstDiff < n) {
                            std::cerr << "；首个差异顶点 #" << firstDiff
                                << " 录(" << pf->frag.verts[firstDiff].x << "," << pf->frag.verts[firstDiff].y
                                << ") 现(" << fresh.verts[firstDiff].x << "," << fresh.verts[firstDiff].y << ")";
                        } else {
                            std::cerr << "；前 " << n << " 个顶点相同，长度不同";
                        }
                        std::cerr << "\n";
                        if (std::getenv("ZEKIT_VERIFY_DUMP")) {
                            std::cerr << "    录:";
                            for (const auto& v : pf->frag.verts)
                                std::cerr << " (" << v.x << "," << v.y << ")";
                            std::cerr << "\n    现:";
                            for (const auto& v : fresh.verts)
                                std::cerr << " (" << v.x << "," << v.y << ")";
                            std::cerr << "\n";
                        }
                    }
                }
            }
            if (bad) {
                std::cerr << "[Zekit/verify] ★ 本帧 " << checked << " 个片段中 "
                    << bad << " 个过期（有状态改了没标脏）\n";
            }
        }

        static bool SameFragment(const FrameCollector::Fragment& a,
                                 const FrameCollector::Fragment& b) {
            if (a.idx.size() != b.idx.size()) return false;
            if (a.verts.size() != b.verts.size()) return false;
            if (a.batches.size() != b.batches.size()) return false;
            if (!a.verts.empty() && std::memcmp(a.verts.data(), b.verts.data(),
                    a.verts.size() * sizeof(UIVertex)) != 0) return false;
            if (!a.idx.empty() && std::memcmp(a.idx.data(), b.idx.data(),
                    a.idx.size() * sizeof(uint32_t)) != 0) return false;
            for (std::size_t i = 0; i < a.batches.size(); ++i) {
                const auto& x = a.batches[i];
                const auto& y = b.batches[i];
                if (x.texture != y.texture || x.indexOffset != y.indexOffset
                    || x.indexCount != y.indexCount || x.isColor != y.isColor) return false;
            }
            return true;
        }

        Widget*    m_hover = nullptr;
        Widget*    m_pressed = nullptr;
        Widget*    m_focused = nullptr;

        static constexpr const char* kAutoTooltipName = "__zekit_auto_tooltip";
        Widget* m_tipTarget = nullptr;
        int     m_tipDelay = 400;
        bool    m_tipEnabled = true;

        Tooltip* findAutoTooltip() {
            if (!m_ownerWindow) return nullptr;
            return m_ownerWindow->FindWidgetDeep<Tooltip>(kAutoTooltipName);
        }

        Tooltip* ensureAutoTooltip() {
            if (!m_ownerWindow) return nullptr;
            if (Tooltip* t = findAutoTooltip()) return t;
            Tooltip fresh;
            fresh.SetObjectName(kAutoTooltipName);
            fresh.SetShowDelay(0);
            Tooltip& placed = m_ownerWindow->AddWidget(std::move(fresh));
            return &placed;
        }

        void syncAutoTooltip() {
            if (!m_ownerWindow) return;

            if (m_tipTarget && m_tipTarget->window() != m_ownerWindow)
                m_tipTarget = nullptr;

            if (!m_tipEnabled) {
                if (Tooltip* tip = findAutoTooltip()) tip->Dismiss();
                m_tipTarget = nullptr;
                return;
            }

            Widget* want = nullptr;
            for (Widget* w = m_hover; w; w = w->parent()) {
                if (w->hasTooltip()) { want = w; break; }
            }

            Tooltip* tip = findAutoTooltip();
            if (!want) {
                m_tipTarget = nullptr;
                if (tip) tip->Dismiss();
                return;
            }

            if (!tip) {
                m_tipTarget = nullptr;
                ensureAutoTooltip();
                if (m_ownerWindow) m_ownerWindow->RequestRedraw();
                syncAutoTooltip();
                return;
            }

            if (want == m_tipTarget) return;

            m_tipTarget = want;
            TooltipContent c;
            c.title = want->tooltipTitle();
            c.text = want->tooltipText();
            c.icon = want->tooltipIcon();
            tip->RequestShowFor(*want, std::move(c), m_tipDelay);
        }

        float       m_pointerX = 0.0f;
        float       m_pointerY = 0.0f;
        bool        m_pointerInside = false;
        bool        m_hoverStale = false;
        std::array<Widget*, kKeySlots> m_keyDownTarget{};
        MouseButton m_pressedButton = MouseButton::None;

        Kit::Cursor m_cursor = Kit::Cursor::Arrow;

        uint32_t m_primaryPointerId = 0;
        bool     m_primaryActive = false;
        Widget*  m_touchTarget = nullptr;

        float m_softKeyboardHeight = 0.0f;
        bool  m_softKeyboardVisible = false;
        std::function<void(AppLifecycle)> m_lifecycleFn;

        std::function<void(float, float, float, float)> m_safeAreaFn;
        float m_lastSaL = -1.0f, m_lastSaT = -1.0f;
        float m_lastSaR = -1.0f, m_lastSaB = -1.0f;

        bool m_hasIconBig = false;
        bool m_hasIconSmall = false;
        Window*    m_ownerWindow = nullptr;

        Window::DriveMode m_driveMode = Window::DriveMode::EventDriven;
        int m_frameRate = 60;

        bool m_renderEnabled = true;

        std::chrono::steady_clock::time_point m_wakeAt{};
        bool m_wakePending = false;

        std::chrono::steady_clock::time_point m_lastInputAt{};
        std::chrono::steady_clock::time_point m_lastFrameAt{};

        int m_swapIntervalMode = -1;
        int m_lastSwapSent = -1;

        std::atomic<bool> m_redrawPending{ false };

        struct TimerTask {
            uint64_t id = 0;
            int      intervalMs = 0;
            std::chrono::steady_clock::time_point due{};
            std::function<void()> fn;
            bool     repeat = false;
        };
        std::vector<TimerTask> m_timers;
        uint64_t m_nextTimerId = 1;

        Window* timerRoot() {
            Impl* p = this;
            while (p->m_parentImpl) p = p->m_parentImpl;
            return p->m_ownerWindow;
        }

        bool runDueTimers() {
            if (m_timers.empty()) return false;
            const auto now = std::chrono::steady_clock::now();

            std::vector<uint64_t> dueIds;
            dueIds.reserve(m_timers.size());
            for (const TimerTask& t : m_timers)
                if (t.due <= now) dueIds.push_back(t.id);
            if (dueIds.empty()) return false;

            bool ran = false;
            for (uint64_t id : dueIds) {
                const auto it = std::find_if(m_timers.begin(), m_timers.end(),
                    [id](const TimerTask& t) { return t.id == id; });
                if (it == m_timers.end()) continue;

                std::function<void()> fn = it->fn;
                if (it->repeat) {
                    it->due = now + std::chrono::milliseconds(it->intervalMs);
                } else {
                    m_timers.erase(it);
                }
                if (fn) { fn(); ran = true; }
            }
            return ran;
        }

        int nextTimerWaitMs() const {
            if (m_timers.empty()) return -1;
            auto soonest = m_timers.front().due;
            for (const TimerTask& t : m_timers)
                if (t.due < soonest) soonest = t.due;
            const auto d = std::chrono::duration_cast<std::chrono::milliseconds>(
                soonest - std::chrono::steady_clock::now()).count();
            return d < 0 ? 0 : static_cast<int>(d);
        }

        std::mutex m_postMutex;
        std::vector<std::function<void()>> m_posts;

        bool drainPosts() {
            std::vector<std::function<void()>> batch;
            {
                std::lock_guard<std::mutex> lock(m_postMutex);
                if (m_posts.empty()) return false;
                batch.swap(m_posts);
            }
            for (auto& fn : batch)
                if (fn) fn();
            return true;
        }

        bool m_acceptFileDrop = false;
        std::function<void(const std::vector<std::string>&)> m_onFileDrop;
        Widget* m_fileDropWidget = nullptr;

        Widget* m_dropCand = nullptr;

        bool m_dropAccepted = false;

        std::vector<std::string> m_dropFiles;

        Widget* fileDropTargetAt(float x, float y) {
            if (!m_ownerWindow) return nullptr;
            Widget* hit = m_ownerWindow->hitTestTopmost(x, y);
            for (Widget* w = hit; w; w = w->parent())
                if (w->acceptsFileDrop()) return w;
            return nullptr;
        }

        int SwapIntervalForThisFrame() const {
            if (m_swapIntervalMode >= 0) return m_swapIntervalMode;
            if (m_lastInputAt.time_since_epoch().count() == 0) return 1;
            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_lastInputAt).count();
            return (ageMs < 300) ? 0 : 1;
        }

        int  m_minW = 0, m_minH = 0;
        int  m_maxW = 0, m_maxH = 0;
        bool m_centerOnResize = true;
        bool m_borderless = false;
        bool m_fullscreen = false;

        bool m_titleBarVisible = true;
        bool m_minBtnEnabled   = true;
        bool m_maxBtnEnabled   = true;
        bool m_closeBtnEnabled = true;

        bool m_enabled = true;

        std::function<void(Window&)> m_onClosed;

        std::function<bool(Window&)> m_onCloseRequest;

        int m_lastMouseX = -1;
        int m_lastMouseY = -1;

        struct FilterSlot {
            EventFilter* filter = nullptr;
            Window::FilterId id = 0;
        };
        std::vector<FilterSlot> m_filters;
        Window::FilterId        m_nextFilterId = 1;

        Window::FilterId AllocFilterId() { return m_nextFilterId++; }

        void OnPointer(const Plat::Event& e) {
            Window* win = m_ownerWindow;
            if (!win) return;

            const KeyboardModifiers mods = FromPlatMods(e.mods);
            const float x = e.x, y = e.y;

            switch (e.kind) {
            case Plat::EventKind::PointerDown:
                if (!m_primaryActive) {
                    m_primaryActive = true;
                    m_primaryPointerId = e.pointerId;
                    WindowAccess::MouseMove(*win, x, y, mods);
                    WindowAccess::MousePress(*win, x, y, MouseButton::Left, mods);
                    frameAfterInput();
                }
                break;

            case Plat::EventKind::PointerMove:
                if (m_primaryActive && e.pointerId == m_primaryPointerId)
                    WindowAccess::MouseMove(*win, x, y, mods);
                break;

            case Plat::EventKind::PointerUp:
                if (m_primaryActive && e.pointerId == m_primaryPointerId) {
                    m_primaryActive = false;
                    WindowAccess::MouseRelease(*win, x, y, MouseButton::Left, mods);
                    WindowAccess::ClearMouse(*win);
                    frameAfterInput();
                }
                break;

            case Plat::EventKind::PointerCancel:
                if (m_primaryActive) {
                    m_primaryActive = false;
                    WindowAccess::CancelPointerCapture(*win);
                    WindowAccess::ClearMouse(*win);
                    frameAfterInput();
                }
                break;

            default: break;
            }

            TouchEvent te;
            switch (e.kind) {
            case Plat::EventKind::PointerDown: te.phase = TouchPhase::Begin;  break;
            case Plat::EventKind::PointerMove: te.phase = TouchPhase::Update; break;
            case Plat::EventKind::PointerUp:   te.phase = TouchPhase::End;    break;
            default:                           te.phase = TouchPhase::Cancel; break;
            }
            te.pointerId = e.pointerId;
            te.x = x; te.y = y;
            te.pointerCount = e.pointerCount > TouchEvent::kMaxPointers
                ? TouchEvent::kMaxPointers : e.pointerCount;
            for (uint32_t i = 0; i < te.pointerCount; ++i) {
                te.pointers[i].id = e.pointers[i].id;
                te.pointers[i].x = e.pointers[i].x;
                te.pointers[i].y = e.pointers[i].y;
            }
            te.modifiers = mods;
            WindowAccess::Touch(*win, te);
        }

        bool AcceptsFileDropAt(float x, float y) override {
            (void)x; (void)y;
            return m_dropAccepted;
        }

        bool IsOpaqueAt(float x, float y) override {
            Window* win = m_ownerWindow;
            if (!win) return true;

            return Kit::HitTest::Topmost(
                std::span<Widget* const>(m_widgets.data(), m_widgets.size()),
                x, y) != nullptr;
        }

        bool OnPlatformEvent(const Plat::Event& e) override {
            Window* win = m_ownerWindow;
            if (!win) return false;

            if (!m_enabled) {
                switch (e.kind) {
                case Plat::EventKind::MouseMove:
                case Plat::EventKind::MouseLeave:
                case Plat::EventKind::MouseDown:
                case Plat::EventKind::MouseUp:
                case Plat::EventKind::MouseWheel:
                case Plat::EventKind::KeyDown:
                case Plat::EventKind::KeyUp:
                case Plat::EventKind::CharInput:
                    return true;
                default:
                    break;
                }
            }

            switch (e.kind) {
            case Plat::EventKind::Quit:
                m_quit = true;
                return true;

            case Plat::EventKind::Resize: {
                float l = 0, t = 0, r = 0, b = 0;
                win->GetSafeAreaInsets(l, t, r, b);
                if (l != m_lastSaL || t != m_lastSaT || r != m_lastSaR || b != m_lastSaB) {
                    m_lastSaL = l; m_lastSaT = t; m_lastSaR = r; m_lastSaB = b;
                    if (m_safeAreaFn) m_safeAreaFn(l, t, r, b);
                }
                RenderFrame();
                return true;
            }

            case Plat::EventKind::Redraw:
                RenderFrame();
                return true;

            case Plat::EventKind::Wake:
                drainPosts();
                frameAfterInput();
                return true;

            case Plat::EventKind::FileDragEnter:
            case Plat::EventKind::FileDragOver:
            case Plat::EventKind::FileDragLeave:
            case Plat::EventKind::FileDrop: {
                const bool leaving = e.kind == Plat::EventKind::FileDragLeave;

                if (!e.files.empty()) m_dropFiles = e.files;

                auto resolveTarget = [&](float px, float py) -> Widget* {
                    Widget* innermost = fileDropTargetAt(px, py);
                    Widget* took = nullptr;
                    for (Widget* w = innermost; w; w = w->parent()) {
                        if (!w->acceptsFileDrop() || !w->isEnabled()) continue;
                        if (w->OnFileDragEnter(px, py, m_dropFiles)) { took = w; break; }
                        w->RequestWindowRedraw();
                    }
                    if (m_fileDropWidget && m_fileDropWidget != took) {
                        m_fileDropWidget->OnFileDragLeave();
                        m_fileDropWidget->RequestWindowRedraw();
                    }
                    m_fileDropWidget = took;
                    m_dropCand = innermost;
                    if (took) took->RequestWindowRedraw();
                    m_dropAccepted = (took != nullptr) || (m_onFileDrop != nullptr);
                    return took;
                };

                if (leaving) {
                    if (m_fileDropWidget) {
                        m_fileDropWidget->OnFileDragLeave();
                        m_fileDropWidget->RequestWindowRedraw();
                        m_fileDropWidget = nullptr;
                    }
                    m_dropFiles.clear();
                    m_dropCand = nullptr;
                    m_dropAccepted = false;
                    frameAfterInput();
                    return true;
                }

                if (e.kind == Plat::EventKind::FileDrop) {
                    if (!m_fileDropWidget) resolveTarget(e.x, e.y);
                    if (m_fileDropWidget) {
                        m_fileDropWidget->OnFileDrop(e.x, e.y, m_dropFiles);
                        m_fileDropWidget->OnFileDragLeave();
                        m_fileDropWidget->RequestWindowRedraw();
                        m_fileDropWidget = nullptr;
                    } else if (m_onFileDrop) {
                        m_onFileDrop(m_dropFiles);
                    }
                    m_dropFiles.clear();
                    m_dropCand = nullptr;
                    m_dropAccepted = false;
                    frameAfterInput();
                    return true;
                }

                if (e.kind == Plat::EventKind::FileDragEnter) {
                    resolveTarget(e.x, e.y);
                } else {
                    Widget* cand = fileDropTargetAt(e.x, e.y);
                    if (cand != m_dropCand) {
                        resolveTarget(e.x, e.y);
                    } else if (m_fileDropWidget) {
                        m_fileDropWidget->OnFileDragOver(e.x, e.y);
                        m_dropAccepted = true;
                    } else {
                        m_dropAccepted = (m_onFileDrop != nullptr);
                    }
                }

                if (m_fileDropWidget) frameAfterInput();
                return true;
            }

            case Plat::EventKind::Close: {
                if (!m_closeBtnEnabled) return true;
                if (m_onCloseRequest && m_ownerWindow) {
                    std::function<bool(Window&)> ask = m_onCloseRequest;
                    if (!ask(*m_ownerWindow)) return true;
                }
                const bool sharedDeps = hasSharedDependents();
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] Close 放行：提前拆后端="
                              << (sharedDeps ? "否（有共享子窗口）" : "是")
                              << "，后端="
                              << (m_renderBackend ? Rhi::ApiName(m_renderBackend->api()) : "none")
                              << "\n";
                promoteInheritingChildren();

                dismissOwnedSurfaces();

                if (!sharedDeps) shutdownRenderer();
                return false;
            }

            case Plat::EventKind::MouseMove: {
                m_lastMouseX = static_cast<int>(e.x);
                m_lastMouseY = static_cast<int>(e.y);
                WindowAccess::MouseMove(*win, e.x, e.y, FromPlatMods(e.mods));
                return true;
            }

            case Plat::EventKind::MouseLeave:
                m_lastMouseX = -1;
                m_lastMouseY = -1;
                WindowAccess::ClearMouse(*win);
                if (m_cursor != Kit::Cursor::Arrow) {
                    m_cursor = Kit::Cursor::Arrow;
                    if (m_native) m_native->SetMouseCursor(Kit::Cursor::Arrow);
                }
                frameAfterInput();
                return true;

            case Plat::EventKind::MouseDown:
                WindowAccess::MousePress(*win, e.x, e.y, FromPlatButton(e.button), FromPlatMods(e.mods));
                frameAfterInput();
                return true;

            case Plat::EventKind::MouseUp:
                WindowAccess::MouseRelease(*win, e.x, e.y, FromPlatButton(e.button), FromPlatMods(e.mods));
                frameAfterInput();
                return true;

            case Plat::EventKind::MouseWheel:
                WindowAccess::MouseWheel(*win, e.x, e.y, e.wheelDelta, FromPlatMods(e.mods));
                return true;

            case Plat::EventKind::PointerDown:
            case Plat::EventKind::PointerMove:
            case Plat::EventKind::PointerUp:
            case Plat::EventKind::PointerCancel:
                OnPointer(e);
                return true;

            case Plat::EventKind::SurfaceChanged: {
                if (m_renderBackend) {
                    m_renderBackend->DetachTextureSinks(&m_fontRenderer, &m_imageStore);
                    m_renderBackend->Shutdown();
                    m_renderBackend.reset();
                }
                m_nativeHandle = NativeHandle();
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] SurfaceChanged：渲染后端已拆，新句柄="
                    << (m_nativeHandle ? "有" : "**无**（等 INIT_WINDOW）") << "\n";
                if (m_nativeHandle) frameAfterInput();
                return true;
            }

            case Plat::EventKind::SoftKeyboard:
                m_softKeyboardHeight = static_cast<float>(e.height);
                m_softKeyboardVisible = (e.height > 0) || (m_native && m_native->IsSoftKeyboardVisible());
                frameAfterInput();
                return true;

            case Plat::EventKind::AppLifecycleEvent:
                if (e.lifecycle == Plat::AppLifecycle::Paused && m_native)
                    m_native->ShowSoftKeyboard(false);
                if (m_lifecycleFn) m_lifecycleFn(FromPlatLifecycle(e.lifecycle));
                return true;

            case Plat::EventKind::KeyDown:
                WindowAccess::Key(*win, EventType::KeyPress, static_cast<Key>(e.key),
                    e.virtualKey, e.scanCode, e.isRepeat, FromPlatMods(e.mods), 0);
                frameAfterInput();
                return true;

            case Plat::EventKind::KeyUp:
                WindowAccess::Key(*win, EventType::KeyRelease, static_cast<Key>(e.key),
                    e.virtualKey, e.scanCode, false, FromPlatMods(e.mods), 0);
                frameAfterInput();
                return true;

            case Plat::EventKind::CharInput:
                WindowAccess::Key(*win, EventType::CharInput, Key::None,
                    0, 0, false, FromPlatMods(e.mods),
                    static_cast<char32_t>(e.codepoint));
                frameAfterInput();
                return true;

            case Plat::EventKind::FocusIn:
                frameAfterInput();
                return true;

            case Plat::EventKind::FocusOut:
                WindowAccess::ClearMouse(*win);
                m_keyDownTarget.fill(nullptr);
                frameAfterInput();
                return true;

            case Plat::EventKind::ThemeChanged:
                win->refreshDarkModeFromSystem();
                frameAfterInput();
                return true;

            case Plat::EventKind::ImeStart:
                if (Widget* f = m_focused)
                    if (f->OnImeStartComposition()) { frameAfterInput(); return true; }
                return false;

            case Plat::EventKind::ImeCompose:
                if (Widget* f = m_focused)
                    if (f->OnImeComposition(static_cast<uint32_t>(e.nativeData),
                        static_cast<uintptr_t>(e.nativeData2))) {
                        frameAfterInput();
                        return true;
                    }
                return false;

            case Plat::EventKind::ImeEnd:
                if (Widget* f = m_focused)
                    if (f->OnImeEndComposition()) { frameAfterInput(); return true; }
                return false;

            case Plat::EventKind::ImeChar:
                if (Widget* f = m_focused)
                    if (f->OnImeChar(static_cast<wchar_t>(e.codepoint))) { frameAfterInput(); return true; }
                return false;

            case Plat::EventKind::SystemButtonGate: {
                switch (e.systemButton) {
                case Plat::SystemButton::Close:      return !m_closeBtnEnabled;
                case Plat::SystemButton::Minimize:   return !m_minBtnEnabled;
                case Plat::SystemButton::Maximize:   return !m_maxBtnEnabled;
                default:                             return false;
                }
            }

            default:
                return false;
            }
        }

        void OnRequestRedraw(Plat::IWindowHost::RedrawReason reason =
                             Plat::IWindowHost::RedrawReason::System) override {
            if (m_isChild) {
                m_redrawPending.store(false, std::memory_order_relaxed);
                RenderFrame();
                return;
            }

            const bool scheduled = (reason == Plat::IWindowHost::RedrawReason::Scheduled);
            if (m_native && !scheduled) {
                int w = 0, h = 0;
                m_native->GetClientSize(w, h);
                const auto now = std::chrono::steady_clock::now();
                const bool sameSize = (w == m_lastPaintW && h == m_lastPaintH);
                const bool tooSoon = (m_lastPaintAt.time_since_epoch().count() != 0)
                    && (std::chrono::duration<double, std::milli>(now - m_lastPaintAt).count() < 6.0);
                const bool inResizeStream =
                    (m_lastSizeChangeAt.time_since_epoch().count() != 0)
                    && (std::chrono::duration<double, std::milli>(now - m_lastSizeChangeAt).count() < 200.0);
                if (sameSize && (tooSoon || inResizeStream)
                    && !m_redrawPending.load(std::memory_order_acquire))
                    return;
            }
            m_redrawPending.store(false, std::memory_order_relaxed);
            RenderFrame();
        }

        void OnNativeClosed() override {
            m_nativeHandle = nullptr;

            UnregisterHeir(this);
            const bool closedAnchor = m_countsAsAnchor;
            if (m_countsAsAnchor) {
                m_countsAsAnchor = false;
                --AnchorWindowCount();
            }
            if (m_countsAsLive) {
                m_countsAsLive = false;
                const int liveLeft = --LiveWindowCount();
                const bool lastLive = (liveLeft == 0);
                const bool lastAnchor = closedAnchor && (AnchorWindowCount() == 0);
                if ((lastLive || lastAnchor) && ExecLoopCount() > 0) {
                    if (!ExecHosts().empty() && ExecHosts().back())
                        ExecHosts().back()->m_quit = true;
                    if (m_native) m_native->RequestQuit(0);
                    if (Plat::TraceLifecycle() && !lastLive && lastAnchor)
                        std::cerr << "[Zekit/trace] 主窗口全部关闭（还有 " << liveLeft
                                  << " 个附属窗口在场）→ 请求退出消息循环\n";
                }
            }
            if (Plat::TraceLifecycle())
                std::cerr << "[Zekit/trace] OnNativeClosed：存活窗口=" << LiveWindowCount()
                << " 主窗口(锚点)=" << AnchorWindowCount()
                << " exec 循环=" << ExecLoopCount()
                << " 原生窗口=" << (m_native ? "在" : "**已空**")
                << "（存活数归零 **或** 主窗口全关 + 有循环在跑，才发退出请求）\n";

            if (m_onClosed && m_ownerWindow) {
                std::function<void(Window&)> cb = m_onClosed;
                cb(*m_ownerWindow);
            }
        }

        void QuerySizeLimits(int& minW, int& minH, int& maxW, int& maxH) const override {
            minW = m_minW; minH = m_minH; maxW = m_maxW; maxH = m_maxH;
        }

        void OnSystemSettingsChanged() override {
            if (m_ownerWindow) m_ownerWindow->refreshDarkModeFromSystem();
            frameAfterInput();
        }

        void OnHostGeometryChanged() override {
            if (!m_native || m_native->IsClosed()) return;

            int ox = 0, oy = 0, w = 0, h = 0;
            m_native->ClientOriginToScreen(ox, oy);
            m_native->GetClientSize(w, h);
            if (m_geomValid && ox == m_geomOx && oy == m_geomOy
                && w == m_geomW && h == m_geomH)
                return;
            m_geomValid = true;
            m_geomOx = ox; m_geomOy = oy; m_geomW = w; m_geomH = h;

            if (!m_ownerWindow) return;

            OverlayHost::NotifyGeometryChanged(*m_ownerWindow);

            m_ownerWindow->VisitTree([](Widget& w, int) {
                if (auto* cv = dynamic_cast<CanvasView*>(&w)) cv->SyncToHost();
            });
        }

        static KeyboardModifiers FromPlatMods(Plat::Modifiers m) {
            KeyboardModifiers out = KeyboardModifiers::NoModifier;
            if (Plat::Has(m, Plat::Modifiers::Shift))   out |= KeyboardModifiers::Shift;
            if (Plat::Has(m, Plat::Modifiers::Control)) out |= KeyboardModifiers::Control;
            if (Plat::Has(m, Plat::Modifiers::Alt))     out |= KeyboardModifiers::Alt;
            return out;
        }
        static MouseButton FromPlatButton(Plat::MouseButton b) {
            switch (b) {
            case Plat::MouseButton::Left:   return MouseButton::Left;
            case Plat::MouseButton::Right:  return MouseButton::Right;
            case Plat::MouseButton::Middle: return MouseButton::Middle;
            case Plat::MouseButton::X1:     return MouseButton::X1;
            case Plat::MouseButton::X2:     return MouseButton::X2;
            default:                        return MouseButton::None;
            }
        }
        static AppLifecycle FromPlatLifecycle(Plat::AppLifecycle l) {
            switch (l) {
            case Plat::AppLifecycle::Resumed:     return AppLifecycle::Resumed;
            case Plat::AppLifecycle::Paused:      return AppLifecycle::Paused;
            case Plat::AppLifecycle::LowMemory:   return AppLifecycle::LowMemory;
            case Plat::AppLifecycle::Terminating: return AppLifecycle::Terminating;
            default:                              return AppLifecycle::None;
            }
        }

        bool m_inFrame = false;

        static constexpr int kGlyphUploadsPerFrame = 256;

        bool m_glyphWakeInstalled = false;

        bool m_firstFramePatched = false;

        std::shared_ptr<int> m_aliveToken = std::make_shared<int>(0);

        struct FrameGuard {
            bool& flag;
            explicit FrameGuard(bool& f) : flag(f) { flag = true; }
            ~FrameGuard() { flag = false; }
            FrameGuard(const FrameGuard&) = delete;
            FrameGuard& operator=(const FrameGuard&) = delete;
        };

        void shutdownRenderer() {
            if (m_renderBackend) {
                m_renderBackend->DetachTextureSinks(&m_fontRenderer, &m_imageStore);
                m_renderBackend->Shutdown();
                m_renderBackend.reset();
            }
            m_fontRenderer.Shutdown();
        }

        void syncBackdropColor() {
            if (!m_native) return;
            const Color4f& bg = m_theme.bg;
            const auto ch = [](float v) {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                return static_cast<unsigned int>(v * 255.0f + 0.5f);
            };
            m_native->SetBackdropColor((ch(bg.r) << 16) | (ch(bg.g) << 8) | ch(bg.b));
        }

        void applyTheme(const Theme& t) {
            m_theme = t;
            syncBackdropColor();

            ++m_themeEpoch;

            if (Plat::TraceTheme())
                std::cerr << "[Zekit/theme] ★ applyTheme：画布底色 " << ThemeHex(m_theme.bg)
                << "（每帧 clear 用的就是它）\n";
        }

        Theme themeForPolarity(bool dark) const {
            return m_userTheme ? Themes::ForDarkMode(m_baseTheme, dark)
                               : Themes::ForDarkMode(dark);
        }

        void applyDarkMode(bool dark, bool force = false) {
            const bool changed = (dark != m_dark);
            m_dark = dark;
            if (m_ownerWindow) m_ownerWindow->applyTitleBarDarkMode();
            if (!changed && !force) return;

            applyTheme(themeForPolarity(dark));
            if (m_ownerWindow) {
                for (Widget* w : m_widgets)
                    if (w) m_ownerWindow->applyThemeToAll(*w);
            }
            if (m_ownerWindow) m_ownerWindow->RequestRedraw();
            propagateThemeToDependents();
        }

        void propagateThemeToDependents() {
            if (m_dependents.empty()) return;

            std::vector<Impl*> kids = m_dependents;
            for (Impl* kid : kids)
                if (kid) kid->inheritThemeFrom(*this);
        }

        void inheritThemeFrom(const Impl& parent) {
            m_dark = parent.m_dark;
            applyTheme(m_userTheme ? themeForPolarity(m_dark) : parent.m_theme);
            if (m_ownerWindow) {
                for (Widget* w : m_widgets)
                    if (w) m_ownerWindow->applyThemeToAll(*w);
                m_ownerWindow->RequestRedraw();
            }
            propagateThemeToDependents();
            frameAfterInput();
        }

        void RenderFrame() {
            if (m_inFrame) return;
            if (!m_native || m_native->IsClosed()) return;
            if (!m_renderEnabled) return;
            if (!ensureBackend()) return;

            const bool traceFrame = Plat::TraceFrame();
            const auto tFrame0 = std::chrono::steady_clock::now();
            auto msSince = [](std::chrono::steady_clock::time_point t0) {
                return std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                };
            double msAuto = 0.0, msFill = 0.0, msPaint = 0.0, msDraw = 0.0;
            double inputAgeAtStart = -1.0;
            std::size_t paintedNodes = 0;

            int W = 0, H = 0;
            m_native->GetClientSize(W, H);
            if (W <= 0 || H <= 0) return;

            const FrameGuard frameGuard(m_inFrame);
            const FrameDepthGuard frameDepthGuard;
            m_frameRequests.store(0, std::memory_order_relaxed);
            m_frameRequestLogs = 0;

            if (m_lastInputAt.time_since_epoch().count() != 0)
                inputAgeAtStart = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - m_lastInputAt).count();

            m_frameAllDirty = m_allPaintDirty.exchange(false, std::memory_order_acq_rel);
            if (m_frameAllDirty)
                markAllPaintDirty();

            if (!m_glyphWakeInstalled) {
                m_fontRenderer.SetWakeCallback([this]() {
                    if (m_native && !m_native->IsClosed()) {
                        m_redrawPending.store(true, std::memory_order_release);
                        m_native->RequestRedraw();
                    }
                    });
                m_glyphWakeInstalled = true;
            }
            m_fontRenderer.PumpReadyGlyphs(kGlyphUploadsPerFrame);

            m_geometrySync = true;
            m_framePhase = 1;

            if (W != m_lastPaintW || H != m_lastPaintH)
                m_hoverStale = true;
            {
                auto autoSizeTree = [](auto&& self, Widget* w, bool arrangedByParent) -> void {
                    if (!w || !w->isVisible()) return;

                    if (!arrangedByParent && !w->fillWindow()) {
                        const bool implicitFill =
                            w->isTopLevel() && dynamic_cast<LayoutBase*>(w) &&
                            w->prefWidth() <= 0.0f && w->prefHeight() <= 0.0f;
                        if (!implicitFill) w->AutoSize();
                    }

                    const bool arranges = w->arrangesChildren();
                    for (Widget* c : w->children())
                        self(self, c, arranges);
                };
                for (Widget* w : m_widgets)
                    autoSizeTree(autoSizeTree, w, false);
            }
            if (traceFrame) msAuto = msSince(tFrame0);

            for (Widget* w : m_widgets) {
                if (!w || !w->fillWindow()) continue;
                const float cw = static_cast<float>(W), ch = static_cast<float>(H);
                if (w->x() != 0.0f || w->y() != 0.0f ||
                    w->width() != cw || w->height() != ch)
                    w->SetGeometry(0.0f, 0.0f, cw, ch);
            }

            for (Widget* w : m_widgets) {
                if (!w || !w->isVisible() || w->fillWindow()) continue;
                if (!dynamic_cast<LayoutBase*>(w)) continue;
                if (w->prefWidth() > 0.0f || w->prefHeight() > 0.0f) continue;

                const float cw = static_cast<float>(W), ch = static_cast<float>(H);
                if (w->x() != 0.0f || w->y() != 0.0f ||
                    w->width() != cw || w->height() != ch) {
                    w->SetPosition(0.0f, 0.0f);
                    LayoutAccess::SetActual(*w, cw, ch);
                }
            }

            if (m_hoverStale) {
                m_hoverStale = false;

                auto ensureTree = [](auto&& self, Widget* w) -> void {
                    if (!w || !w->isVisible()) return;
                    if (!w->children().empty())
                        if (auto* lb = dynamic_cast<LayoutBase*>(w)) lb->EnsureLayout();
                    for (Widget* c : w->children()) self(self, c);
                };
                for (Widget* w : m_widgets) ensureTree(ensureTree, w);

                if (m_ownerWindow) WindowAccess::ResyncHover(*m_ownerWindow);
            }

            m_geometrySync = false;
            m_framePhase = 2;
            if (traceFrame) msFill = msSince(tFrame0) - msAuto;
            FrameCollector& fc = m_collector;
            fc.Clear();

            m_fontRenderer.SetScale(m_native->RenderScale());

            fc.SetFontRenderer(&m_fontRenderer);
            fc.SetImageStore(&m_imageStore);
            fc.SetTheme(&m_theme);

            m_recorder.SetFontRenderer(&m_fontRenderer);
            m_recorder.SetImageStore(&m_imageStore);
            m_recorder.SetTheme(&m_theme);

            std::vector<Widget*> overlayRoots;

            PaintEnv env;
            env.theme = &m_theme;
            env.themeEpoch = m_themeEpoch;
            env.glyphDropEpoch = m_fontRenderer.DropEpoch();
            env.glyphContentEpoch = m_fontRenderer.ContentEpoch();

            auto culledNodes = std::size_t{ 0 };
            auto reusedNodes = std::size_t{ 0 };
            auto recordedNodes = std::size_t{ 0 };
            auto syncNativeDeep = [](auto&& self, Widget* w) -> void {
                if (!w) return;
                if (w->hasNativeSurface()) w->syncNativeSurface();
                for (Widget* c : w->children()) self(self, c);
            };

            auto paintTree = [&](auto&& self, Widget* w, bool overlayPass) -> void {
                if (!w) return;

                if (w->hasNativeSurface()) {
                    m_sawNativeSurface = true;
                    w->syncNativeSurface();
                }

                if (!w->isVisible()) {
                    if (m_sawNativeSurface)
                        for (Widget* c : w->children()) syncNativeDeep(syncNativeDeep, c);
                    return;
                }
                ++paintedNodes;

                if (!w->children().empty()) {
                    if (auto* lb = dynamic_cast<LayoutBase*>(w))
                        lb->EnsureLayout();
                }

                const bool onTop = w->paintsOnTop();
                if (!overlayPass && onTop) {
                    overlayRoots.push_back(w);
                    return;
                }

                const float wx = w->x(), wy = w->y(), ww = w->width(), wh = w->height();
                float clx = 0.0f, cly = 0.0f, clw = 0.0f, clh = 0.0f;
                const bool hasClip = fc.CurrentClip(clx, cly, clw, clh);

                bool outsideView = false;
                if (ww > 0.0f && wh > 0.0f) {
                    float vx0 = 0.0f, vy0 = 0.0f;
                    float vx1 = static_cast<float>(W), vy1 = static_cast<float>(H);
                    if (hasClip) {
                        if (clx > vx0) vx0 = clx;
                        if (cly > vy0) vy0 = cly;
                        if (clx + clw < vx1) vx1 = clx + clw;
                        if (cly + clh < vy1) vy1 = cly + clh;
                        if (vx1 <= vx0 || vy1 <= vy0) outsideView = true;
                    }
                    if (!outsideView) {
                        outsideView = (wx >= vx1) || (wy >= vy1) ||
                                      (wx + ww <= vx0) || (wy + wh <= vy0);
                    }
                }

                if (outsideView) {
                    ++culledNodes;
                    if (TracePaint() && paintedNodes < 400)
                        std::cerr << "[paint] CULL   " << typeid(*w).name()
                        << " " << wx << "," << wy << " " << ww << "x" << wh << "\n";
                }
                else {
                    const bool animating = w->wantsAnimationFrame();
                    const bool cacheable = !animating && !onTop && PaintCacheOff() == false;
                    PaintFragment* pf = cacheable ? PaintCacheAccess::Get(*w) : nullptr;

                    if (pf && !PaintCacheAccess::IsDirty(*w)
                        && pf->Reusable(env, animating, wx, wy, ww, wh,
                                        hasClip, clx, cly, clw, clh)) {
                        fc.AppendFragment(pf->frag);
                        ++reusedNodes;
                        if (TracePaint() && paintedNodes < 400)
                            std::cerr << "[paint] REUSE  " << typeid(*w).name()
                            << " " << wx << "," << wy << " " << ww << "x" << wh
                            << " 顶点 " << pf->frag.verts.size() << "\n";
                    }
                    else if (!cacheable || m_frameAllDirty) {
                        if (pf) pf->Invalidate();
                        w->Paint(fc);
                        if (TracePaint() && paintedNodes < 400)
                            std::cerr << "[paint] DIRECT " << typeid(*w).name()
                            << " " << wx << "," << wy << " " << ww << "x" << wh
                            << (animating ? " anim" : "")
                            << (m_frameAllDirty ? " allDirty" : "")
                            << (cacheable ? "" : " noCache") << "\n";
                    }
                    else {
                        PaintFragment& out = PaintCacheAccess::Ensure(*w);
                        m_recorder.BeginCapture(out.frag);
                        if (hasClip && clw > 0.0f && clh > 0.0f)
                            m_recorder.PushClip(clx, cly, clw, clh);

                        const uint64_t miss0 = m_fontRenderer.MissCount();
                        PaintCacheAccess::SetTimed(*w, false);
                        m_paintingWidget = w;
                        w->Paint(m_recorder);
                        m_paintingWidget = nullptr;

                        const bool clipClean = (m_recorder.ClipDepth() == (hasClip ? 1 : 0));

                        m_recorder.EndCapture(out.frag);
                        out.x = wx; out.y = wy; out.w = ww; out.h = wh;

                        out.hasClip = hasClip;
                        out.clipX = clx; out.clipY = cly;
                        out.clipW = clw; out.clipH = clh;
                        out.env = env;
                        out.hadMissingGlyph = (m_fontRenderer.MissCount() != miss0);
                        out.timed = PaintCacheAccess::Timed(*w);
                        out.valid = clipClean;
                        PaintCacheAccess::ClearDirty(*w);
                        ++recordedNodes;
                        fc.AppendFragment(out.frag);
                        if (TracePaint() && paintedNodes < 400)
                            std::cerr << "[paint] RECORD " << typeid(*w).name()
                            << " " << wx << "," << wy << " " << ww << "x" << wh
                            << " 顶点 " << out.frag.verts.size()
                            << " valid=" << out.valid << "\n";
                    }
                }

                float cx = 0, cy = 0, cw = 0, ch = 0;
                const bool clip = w->clipChildrenRect(cx, cy, cw, ch);
                if (clip) fc.PushClip(cx, cy, cw, ch);
                const std::size_t kidCount = w->children().size();
                for (std::size_t i = 0; i < kidCount; ++i) {
                    const std::vector<Widget*>& kids = w->children();
                    if (i >= kids.size()) break;
                    self(self, kids[i], overlayPass);
                }
                if (clip) fc.PopClip();
            };

            for (Widget* w : m_widgets)
                paintTree(paintTree, w, false);
            const std::vector<Widget*> overlayRootsCopy = overlayRoots;
            for (Widget* w : overlayRootsCopy)
                paintTree(paintTree, w, true);

            if (!m_firstFramePatched && m_ownerWindow) {
                m_firstFramePatched = true;
                Window* w = m_ownerWindow;
                std::weak_ptr<int> alive = m_aliveToken;
                w->SetTimeout(80, [w, alive] {
                    if (alive.expired()) return;
                    w->RequestRedraw();
                });
            }

            if (traceFrame) msPaint = msSince(tFrame0) - msAuto - msFill;

            if (const char* dumpPath = std::getenv("ZEKIT_DUMP_FRAME")) {
                FILE* df = std::fopen(dumpPath, "wb");
                if (df) {
                    const auto& vs = fc.Vertices();
                    const auto& ix = fc.Indices();
                    const auto& bs = fc.Batches();
                    const uint64_t nv = vs.size(), ni = ix.size(), nb = bs.size();
                    std::fwrite(&nv, sizeof(nv), 1, df);
                    std::fwrite(vs.data(), sizeof(UIVertex), nv, df);
                    std::fwrite(&ni, sizeof(ni), 1, df);
                    std::fwrite(ix.data(), sizeof(uint32_t), ni, df);
                    std::fwrite(&nb, sizeof(nb), 1, df);
                    std::fwrite(bs.data(), sizeof(FrameCollector::Batch), nb, df);
                    std::fclose(df);
                }
            }

            const Color4f& canvas = m_theme.bg;
            if (Plat::TraceTheme()) {
                static unsigned lastRgb = 0xFFFFFFFFu;
                const auto ch = [](float v) {
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    return static_cast<unsigned>(v * 255.0f + 0.5f);
                    };
                const unsigned rgb = (ch(canvas.r) << 16) | (ch(canvas.g) << 8) | ch(canvas.b);
                if (rgb != lastRgb) {
                    lastRgb = rgb;
                    std::cerr << "[Zekit/theme] ★ 本帧清屏色 " << ThemeHex(canvas)
                        << "（" << W << "x" << H << "，交给后端的 r/g/b="
                        << canvas.r << "," << canvas.g << "," << canvas.b << "）\n";
                }
            }
            m_lastSwapSent = SwapIntervalForThisFrame();
            m_renderBackend->SetSwapInterval(m_lastSwapSent);
            m_renderBackend->DrawFrame(fc, W, H, canvas.r, canvas.g, canvas.b);

            if (m_renderBackend->ResizeDeferred() && m_native && !m_native->IsClosed())
                m_native->RequestWakeup(120);

            verifyFragments();

            if (W != m_lastPaintW || H != m_lastPaintH)
                m_lastSizeChangeAt = std::chrono::steady_clock::now();
            m_lastPaintW = W;
            m_lastPaintH = H;
            m_lastPaintAt = std::chrono::steady_clock::now();

            if (traceFrame) {
                msDraw = msSince(tFrame0) - msAuto - msFill - msPaint;
                const auto nowT = std::chrono::steady_clock::now();
                double frameGapMs = -1.0;
                if (m_lastFrameAt.time_since_epoch().count() != 0)
                    frameGapMs = std::chrono::duration<double, std::milli>(nowT - m_lastFrameAt).count();
                m_lastFrameAt = nowT;

                std::cerr << "[Zekit/frame] 总 " << msSince(tFrame0) << "ms"
                    << " | 尺寸 " << W << "x" << H
                    << " | auto " << msAuto << " | fill " << msFill
                    << " | paint " << msPaint << " | draw " << msDraw
                    << " | 间隔 " << frameGapMs << " | 输入年龄 " << inputAgeAtStart
                    << " | 交换间隔 " << m_lastSwapSent
                    << " | 请求 " << m_frameRequests.load()
                    << " | 节点 " << paintedNodes
                    << " 剔除 " << culledNodes
                    << " 复用 " << reusedNodes
                    << " 录制 " << recordedNodes
                    << " 顶点 " << fc.Vertices().size()
                    << " 批次 " << fc.Batches().size()
                    << " | " << W << "x" << H << "\n";
            }
        }

        void frameAfterInput() {
            if (m_driveMode == Window::DriveMode::Immediate) return;
            if (m_native && !m_native->IsClosed()) {
                m_redrawPending.store(true, std::memory_order_release);
                m_native->RequestRedraw();
            }
        }

        bool anyWidgetWantsFrame() const {
            std::vector<const Widget*> stack;
            stack.reserve(m_widgets.size());
            for (Widget* w : m_widgets) if (w) stack.push_back(w);
            while (!stack.empty()) {
                const Widget* w = stack.back();
                stack.pop_back();
                if (!w || !w->isVisible()) continue;
                if (w->wantsAnimationFrame()) return true;
                for (Widget* c : w->children()) if (c) stack.push_back(c);
            }
            return false;
        }

        bool isAliveInWindow(const Widget* w) const {
            if (!w) return false;
            for (const Widget* p = w; p; p = p->parent()) {
                if (!p->parent()) {
                    const auto& v = m_widgets;
                    return std::find(v.begin(), v.end(), p) != v.end();
                }
            }
            return false;
        }

        bool m_quit = false;

        bool ensurePump() {
            if (m_pumpStarted) return true;
            m_pumpStarted = true;

            m_fontRenderer.Init();

            if (!m_renderEnabled) {
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] 单步驱动：渲染权在外部，只泵消息\n";
                resetPacing();
                return true;
            }

            if (!ensureBackend()) {
                m_pumpStarted = false;
                if (!m_pumpInitWarned) {
                    m_pumpInitWarned = true;
                    std::cerr << "[Zekit] no usable render backend; "
                                 "PollEvents will keep pumping messages only.\n";
                }
                return false;
            }

            if (m_driveMode != Window::DriveMode::EventDriven) acquireTimerPrecision();

            resetPacing();
            RenderFrame();
            return true;
        }

        void acquireTimerPrecision() {
            if (m_timerPrecisionHeld || !m_native) return;
            m_native->SetTimerPrecision(true);
            m_timerPrecisionHeld = true;
        }

        void resetPacing() {
            m_paceT0 = std::chrono::steady_clock::now();
            m_paceNextFrame = 1;
            m_paceWasPaced = false;
        }

        bool pumpOnce(int waitCapMs) {
            if (m_quit) return false;
            if (!m_native) { m_quit = true; return false; }

            if (m_native->IsClosed() && LiveWindowCount() == 0) {
                m_quit = true;
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] 平台层已收摊（原生窗口已销毁、"
                                 "无存活窗口）→ 消息循环收工\n";
                return false;
            }

            const bool traceLife = Plat::TraceLifecycle() && TraceLoopVerbose();
            if (traceLife)
                std::cerr << "[Zekit/trace] 消息循环第 " << ++m_pumpRound
                << " 轮：等事件…\n";

            const bool immediate = (m_driveMode == Window::DriveMode::Immediate);
            const bool onDemand = (m_driveMode == Window::DriveMode::OnDemand);
            const auto frameDelta = std::chrono::milliseconds(1000 / std::max(1, m_frameRate));

            const bool heirWants = AnyHeirWantsFrame();
            const bool pacedNow = immediate
                || (onDemand && (anyWidgetWantsFrame() || heirWants));

            int waitMs = -1;
            if (pacedNow) {
                const auto target = m_paceT0 + frameDelta * m_paceNextFrame;
                waitMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        target - std::chrono::steady_clock::now()).count());
                if (waitMs < 0) waitMs = 0;
            }
            if (m_wakePending) {
                const int wakeMs = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        m_wakeAt - std::chrono::steady_clock::now()).count());
                const int w = wakeMs < 0 ? 0 : wakeMs;
                if (waitMs < 0 || w < waitMs) waitMs = w;
            }
            {
                const int timerMs = nextTimerWaitMs();
                if (timerMs >= 0 && (waitMs < 0 || timerMs < waitMs)) waitMs = timerMs;
            }
            if (waitCapMs >= 0 && (waitMs < 0 || waitMs > waitCapMs)) waitMs = waitCapMs;

            m_native->WaitEvents(waitMs);

            int traceDrained = 0;
            while (m_native->PollEvent()) {
                ++traceDrained;
                if (m_quit) break;
            }
            if (traceLife)
                std::cerr << "[Zekit/trace] 消息循环第 " << m_pumpRound << " 轮：抽了 "
                << traceDrained << " 条，m_quit=" << m_quit << "\n";
            if (m_quit) return false;

            {
                const bool ranTimers = runDueTimers();
                const bool ranPosts = drainPosts();
                if (ranTimers || ranPosts)
                    m_redrawPending.store(true, std::memory_order_release);
            }

            if (!pacedNow && m_redrawPending.exchange(false, std::memory_order_acq_rel)) {
                RenderFrame();
                if (traceLife)
                    std::cerr << "[Zekit/trace] 事件批补帧（第 " << m_pumpRound
                    << " 轮，抽了 " << traceDrained << " 条）\n";
            }

            if (pacedNow) {
                if (!m_paceWasPaced) {
                    m_paceT0 = std::chrono::steady_clock::now();
                    m_paceNextFrame = 1;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= m_paceT0 + frameDelta * m_paceNextFrame) {
                    RenderFrame();
                    RenderHeirFrames(immediate);
                    do { ++m_paceNextFrame; }
                    while (m_paceT0 + frameDelta * m_paceNextFrame <= now);
                }
            }
            else if (m_wakePending &&
                std::chrono::steady_clock::now() >= m_wakeAt) {
                m_wakePending = false;
                if (traceLife)
                    std::cerr << "[Zekit/trace] 定时唤醒到点 → 补一帧（第 "
                    << m_pumpRound << " 轮）\n";
                RenderFrame();
            }
            else if (traceLife) {
                std::cerr << "[Zekit/trace] 本轮不画：pacedNow=" << pacedNow
                    << " wakePending=" << m_wakePending << "\n";
            }

            for (Impl* kid : m_dependents) {
                if (!kid || kid->NativeClosed()) continue;
                const bool wakeDue = kid->m_wakePending &&
                    std::chrono::steady_clock::now() >= kid->m_wakeAt;
                if (wakeDue) kid->m_wakePending = false;
                if (wakeDue || kid->m_redrawPending.exchange(false,
                        std::memory_order_acq_rel))
                    kid->RenderFrame();
            }

            m_paceWasPaced = pacedNow;
            return true;
        }

        bool m_pumpStarted = false;
        bool m_pumpInitWarned = false;
        bool m_timerPrecisionHeld = false;
        std::chrono::steady_clock::time_point m_paceT0{};
        long long m_paceNextFrame = 1;
        bool m_paceWasPaced = false;
        int m_pumpRound = 0;

        ~Impl() {
            if (m_timerPrecisionHeld && m_native) m_native->SetTimerPrecision(false);
        }
    };

    Window::FilterId Window::InstallEventFilter(EventFilter& filter) {
        for (const Impl::FilterSlot& slot : m_impl->m_filters) {
            if (slot.filter == &filter) return slot.id;
        }

        const FilterId id = m_impl->AllocFilterId();
        m_impl->m_filters.push_back(Impl::FilterSlot{ &filter, id });
        filter.addInstall(this, id);
        return id;
    }

    void Window::detachEventFilter(FilterId id) {
        auto& v = m_impl->m_filters;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->id != id) continue;
            if (it->filter) it->filter->forgetInstall(this);
            v.erase(it);
            return;
        }
    }

    void Window::RemoveEventFilter(FilterId id) {
        if (id == 0) return;
        detachEventFilter(id);
    }

    bool Window::runMouseFilters(MouseEvent& e) {
        bool blocked = false;
        for (const Impl::FilterSlot& slot : m_impl->m_filters) {
            if (slot.filter && slot.filter->onMouse(*this, e)) blocked = true;
        }
        return blocked;
    }

    bool Window::runKeyFilters(KeyEvent& e) {
        bool blocked = false;
        for (const Impl::FilterSlot& slot : m_impl->m_filters) {
            if (slot.filter && slot.filter->onKey(*this, e)) blocked = true;
        }
        return blocked;
    }

    EventFilter::~EventFilter() {
        while (!installs_.empty()) {
            const Install rec = installs_.back();
            if (rec.window)      rec.window->detachEventFilter(rec.id);
            else if (rec.widget) rec.widget->detachEventFilter(rec.id);
            else                 installs_.pop_back();
        }
    }

    void EventFilter::forgetInstall(Window* w) {
        auto& v = installs_;
        v.erase(std::remove_if(v.begin(), v.end(),
            [w](const Install& r) { return r.window == w; }), v.end());
    }

    void EventFilter::forgetInstall(Widget* w) {
        auto& v = installs_;
        v.erase(std::remove_if(v.begin(), v.end(),
            [w](const Install& r) { return r.widget == w; }), v.end());
    }

    void Window::SetTooltipDelay(int ms) {
        if (!m_impl) return;
        m_impl->m_tipDelay = (ms > 0) ? ms : 0;
    }

    int Window::tooltipDelay() const {
        return m_impl ? m_impl->m_tipDelay : 400;
    }

    void Window::SetTooltipEnabled(bool on) {
        if (!m_impl) return;
        m_impl->m_tipEnabled = on;
        if (!on) m_impl->syncAutoTooltip();
    }

    bool Window::isTooltipEnabled() const {
        return m_impl ? m_impl->m_tipEnabled : false;
    }

    void Window::requestHoverResync() {
        if (!m_impl) return;
        m_impl->m_hoverStale = true;
    }

    void Window::resyncHover() {
        if (!m_impl || !m_impl->m_pointerInside) return;

        if (m_impl->m_hover && !m_impl->isAliveInWindow(m_impl->m_hover))
            m_impl->m_hover = nullptr;

        Widget* hit = hitTestTopmost(m_impl->m_pointerX, m_impl->m_pointerY);

        if (hit != m_impl->m_hover) {
            if (m_impl->m_hover) {
                PaintCacheAccess::MarkDirty(*m_impl->m_hover);
                m_impl->m_hover->OnMouseLeave();
            }
            m_impl->m_hover = hit;
            if (m_impl->m_hover) {
                PaintCacheAccess::MarkDirty(*m_impl->m_hover);
                m_impl->m_hover->OnMouseEnter();
            }
        }

        {
            Widget* target = m_impl->m_pressed ? m_impl->m_pressed : hit;
            const Kit::Cursor want = (target && target->isEnabled())
                ? target->cursorAt(m_impl->m_pointerX, m_impl->m_pointerY)
                : Kit::Cursor::Arrow;
            if (want != m_impl->m_cursor) {
                m_impl->m_cursor = want;
                if (m_impl->m_native) m_impl->m_native->SetMouseCursor(want);
            }
        }

        m_impl->syncAutoTooltip();
    }

    void Window::clearMouseState() {
        if (m_impl->m_hover) {
            m_impl->m_hover->OnMouseLeave();
            m_impl->m_hover = nullptr;
        }
        m_impl->m_pointerInside = false;
        m_impl->syncAutoTooltip();
    }

    void Window::dispatchTouch(TouchEvent& e) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();
        if (e.phase == TouchPhase::Begin) {
            m_impl->m_touchTarget = hitTestTopmost(e.x, e.y);
        }
        Widget* target = m_impl->m_touchTarget;
        if (!target) target = hitTestTopmost(e.x, e.y);

        for (Widget* w = target; w; w = w->parent()) {
            bool handled = false;
            switch (e.phase) {
            case TouchPhase::Begin:  handled = w->OnTouchBegin(e);  break;
            case TouchPhase::Update: handled = w->OnTouchUpdate(e); break;
            case TouchPhase::End:    handled = w->OnTouchEnd(e);    break;
            case TouchPhase::Cancel: handled = w->OnTouchCancel(e); break;
            }
            if (handled || e.accepted) break;
        }

        if (e.phase == TouchPhase::End || e.phase == TouchPhase::Cancel)
            m_impl->m_touchTarget = nullptr;
    }

    void Window::cancelPointerCapture() {
        if (m_impl->m_pressed) {
            m_impl->m_pressed->OnMouseLeave();
            m_impl->m_pressed = nullptr;
        }
        m_impl->m_pressedButton = MouseButton::None;
        m_impl->m_touchTarget = nullptr;
    }

    Widget* Window::hitTestTopmost(float x, float y) const {
        const std::vector<Widget*>& roots = m_impl->m_widgets;
        return HitTest::Topmost(std::span<Widget* const>(roots.data(), roots.size()), x, y);
    }

    void Window::dispatchMouseMove(float x, float y, KeyboardModifiers mods) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();

        m_impl->m_pointerX = x;
        m_impl->m_pointerY = y;
        m_impl->m_pointerInside = true;

        MouseEvent e;
        e.type = EventType::MouseMove;
        e.x = x; e.y = y;
        e.modifiers = mods;
        if (runMouseFilters(e)) return;

        const Widget* prevHover = m_impl->m_hover;
        const bool wasPressed = (m_impl->m_pressed != nullptr);

        Widget* hit = hitTestTopmost(x, y);
        if (hit != m_impl->m_hover) {
            if (m_impl->m_hover) {
                PaintCacheAccess::MarkDirty(*m_impl->m_hover);
                m_impl->m_hover->OnMouseLeave();
            }
            m_impl->m_hover = hit;
            if (m_impl->m_hover) {
                PaintCacheAccess::MarkDirty(*m_impl->m_hover);
                m_impl->m_hover->OnMouseEnter();
            }
        }

        m_impl->syncAutoTooltip();

        Widget* target = m_impl->m_pressed ? m_impl->m_pressed : hit;

        {
            const Kit::Cursor want = (target && target->isEnabled())
                ? target->cursorAt(x, y) : Kit::Cursor::Arrow;
            if (want != m_impl->m_cursor) {
                m_impl->m_cursor = want;
                if (m_impl->m_native) m_impl->m_native->SetMouseCursor(want);
            }
        }

        bool consumed = false;
        for (Widget* w = target; w; w = w->parent()) {
            if (w->runMouseFilters(*this, e)) return;
            if (w->OnMouseMove(e) || e.accepted) {
                consumed = true;
                PaintCacheAccess::MarkDirty(*w);
                break;
            }
        }

        if (m_impl->m_hover != prevHover || m_impl->m_pressed || wasPressed || consumed)
            m_impl->frameAfterInput();
    }

    void Window::dispatchMousePress(float x, float y, MouseButton button, KeyboardModifiers mods) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();
        m_impl->m_pointerX = x; m_impl->m_pointerY = y; m_impl->m_pointerInside = true;

        OverlayHost::NotifyHostPressed(*this, x, y);
        MouseEvent e;
        e.type = EventType::MousePress;
        e.x = x; e.y = y;
        e.button = button;
        e.modifiers = mods;
        if (runMouseFilters(e)) return;

        Widget* hit = hitTestTopmost(x, y);

        {
            Widget* focusTarget = nullptr;
            for (Widget* w = hit; w; w = w->parent()) {
                if (w->isFocusTarget()) { focusTarget = w; break; }
            }
            setFocusedWidget(focusTarget);
        }

        for (Widget* w = hit; w; w = w->parent()) {
            if (w->runMouseFilters(*this, e)) return;
        }

        m_impl->m_pressed = hit;
        m_impl->m_pressedButton = button;

        for (Widget* w = hit; w; w = w->parent()) {
            if (w->OnMousePress(e) || e.accepted) { PaintCacheAccess::MarkDirty(*w); break; }
        }
    }

    void Window::dispatchMouseRelease(float x, float y, MouseButton button, KeyboardModifiers mods) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();
        m_impl->m_pointerX = x; m_impl->m_pointerY = y; m_impl->m_pointerInside = true;
        MouseEvent e;
        e.type = EventType::MouseRelease;
        e.x = x; e.y = y;
        e.button = button;
        e.modifiers = mods;

        if (runMouseFilters(e)) {
            m_impl->m_pressed = nullptr;
            m_impl->m_pressedButton = MouseButton::None;
            return;
        }

        Widget* target = m_impl->m_pressed;
        if (!target || m_impl->m_pressedButton != button)
            target = hitTestTopmost(x, y);

        m_impl->m_pressed = nullptr;
        m_impl->m_pressedButton = MouseButton::None;

        for (Widget* w = target; w; w = w->parent()) {
            if (w->runMouseFilters(*this, e)) return;
            if (w->OnMouseRelease(e) || e.accepted) { PaintCacheAccess::MarkDirty(*w); break; }
        }
    }

    void Window::dispatchMouseWheel(float x, float y, int wheelDelta, KeyboardModifiers mods) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();
        m_impl->m_pointerX = x; m_impl->m_pointerY = y; m_impl->m_pointerInside = true;
        MouseEvent e;
        e.type = EventType::MouseWheel;
        e.x = x; e.y = y;
        e.wheelDelta = wheelDelta;
        e.modifiers = mods;
        if (runMouseFilters(e)) return;

        Widget* hit = hitTestTopmost(x, y);

        bool consumed = false;
        for (Widget* w = hit; w; w = w->parent()) {
            if (w->runMouseFilters(*this, e)) return;
            if (w->OnMouseWheel(e) || e.accepted) {
                consumed = true;
                PaintCacheAccess::MarkDirty(*w);
                break;
            }
        }

        if (consumed)
            m_impl->frameAfterInput();
    }

    void Window::dispatchKey(EventType type, Key key, uint32_t virtualKey,
        uint32_t scanCode, bool isRepeat, KeyboardModifiers mods, char32_t ch) {
        m_impl->m_lastInputAt = std::chrono::steady_clock::now();

        KeyEvent e;
        e.type = type;
        e.key = key;
        e.virtualKey = virtualKey;
        e.nativeScanCode = scanCode;
        e.isRepeat = isRepeat;
        e.modifiers = mods;
        e.character = ch;

        if (runKeyFilters(e)) return;

        Widget* focused = focusedWidget();

        if (type == EventType::KeyRelease) {
            Widget*& slot = m_impl->m_keyDownTarget[static_cast<uint32_t>(key) % kKeySlots];
            if (Widget* owner = slot) {
                slot = nullptr;
                if (m_impl->isAliveInWindow(owner)) {
                    bool consumed = false;
                    for (Widget* w = owner; w; w = w->parent()) {
                        switch (type) {
                        case EventType::KeyPress:   consumed = w->OnKeyPress(e);   break;
                        case EventType::KeyRelease: consumed = w->OnKeyRelease(e); break;
                        case EventType::CharInput:  consumed = w->OnCharInput(e);  break;
                        default: break;
                        }
                        if (consumed) PaintCacheAccess::MarkDirty(*w);
                        if (consumed || e.accepted) return;
                    }
                    return;
                }
            }
        }

        if (key == Key::Tab && (type == EventType::KeyPress || type == EventType::KeyRelease)) {
            if (type == EventType::KeyRelease) return;

            std::vector<Widget*> order;
            auto collect = [&order](auto&& self, Widget* w) -> void {
                if (!w || !w->isVisible()) return;
                if (w->isFocusTarget()) order.push_back(w);
                for (Widget* c : w->children()) self(self, c);
            };
            for (Widget* root : m_impl->m_widgets) collect(collect, root);

            if (!order.empty()) {
                const std::size_t n = order.size();
                const bool back = hasModifier(mods, KeyboardModifiers::Shift);

                std::size_t i = 0;
                bool found = false;
                if (focused) {
                    auto it = std::find(order.begin(), order.end(), focused);
                    if (it != order.end()) {
                        i = static_cast<std::size_t>(it - order.begin());
                        found = true;
                    }
                }
                if (found)
                    i = back ? (i + n - 1) % n : (i + 1) % n;
                else
                    i = back ? (n - 1) : 0;
                setFocusedWidget(order[i]);
                return;
            }
        }

        if (focused) {
            for (Widget* w = focused; w; w = w->parent()) {
                bool consumed = false;
                switch (type) {
                case EventType::KeyPress:   consumed = w->OnKeyPress(e);   break;
                case EventType::KeyRelease: consumed = w->OnKeyRelease(e); break;
                case EventType::CharInput:  consumed = w->OnCharInput(e);  break;
                default: break;
                }
                if (consumed || e.accepted) {
                    if (type == EventType::KeyPress && !isRepeat)
                        m_impl->m_keyDownTarget[static_cast<uint32_t>(key) % kKeySlots] = w;
                    return;
                }
            }
            return;
        }

        if (type == EventType::CharInput) return;

        const std::vector<Widget*>& roots = m_impl->m_widgets;
        for (Widget* root : roots) {
            if (!root || !root->isVisible()) continue;

            bool filtered = false;
            for (Widget* w = root; w; w = w->parent()) {
                if (w->runKeyFilters(*this, e)) { filtered = true; break; }
            }
            if (filtered) continue;

            bool consumed = false;
            switch (type) {
            case EventType::KeyPress:   consumed = root->OnKeyPress(e);   break;
            case EventType::KeyRelease: consumed = root->OnKeyRelease(e); break;
            case EventType::CharInput:  consumed = root->OnCharInput(e);  break;
            default: break;
            }
            if (consumed) PaintCacheAccess::MarkDirty(*root);
            if (consumed || e.accepted) {
                if (type == EventType::KeyPress && !isRepeat)
                    m_impl->m_keyDownTarget[static_cast<uint32_t>(key) % kKeySlots] = root;
                return;
            }
        }
    }

    void Window::adoptTop(std::unique_ptr<Widget> p) {
        Widget& ref = *p;

        if (m_impl->m_emulated) {
            ref.parent_ = m_impl->m_emulated;
            if (m_impl->m_parentImpl && m_impl->m_parentImpl->m_ownerWindow)
                ref.setOwner(m_impl->m_parentImpl->m_ownerWindow);
            m_impl->m_emulated->children_.push_back(&ref);
            m_impl->m_emulated->owned_.push_back(std::move(p));
            applyThemeToPristine(ref);
            ref.AutoSize();
            primeChildDisplay();
            return;
        }

        ref.setOwner(this);
        m_impl->m_widgets.push_back(&ref);
        m_impl->m_owned.push_back(std::move(p));

        applyThemeToPristine(ref);

        if (!m_impl->m_acceptFileDrop) {
            bool anyDrop = false;
            ref.VisitTree([&anyDrop](Widget& w, int) {
                if (w.acceptsFileDrop()) anyDrop = true;
            });
            if (anyDrop) SetAcceptFileDrop(true);
        }

        ref.AutoSize();

        primeChildDisplay();
    }

    void Window::primeChildDisplay() {
        if (!m_impl || !m_impl->m_isChild) return;
        if (m_impl->m_childDisplayPrimed) return;
        if (m_impl->m_userHidden) return;
        m_impl->m_childDisplayPrimed = true;
        Show();
    }

    void Window::RemoveWidget(Widget& widget) {
        if (m_impl->m_emulated) {
            m_impl->m_emulated->RemoveWidget(widget);
            return;
        }
        if (m_impl->m_focused) {
            for (Widget* w = m_impl->m_focused; w; w = w->parent()) {
                if (w == &widget) { setFocusedWidget(nullptr); break; }
            }
        }
        auto& v = m_impl->m_widgets;
        v.erase(std::remove(v.begin(), v.end(), &widget), v.end());
        if (widget.owner_ == this) widget.setOwner(nullptr);
    }

    void Window::destroyTop(Widget& w) {
        RemoveWidget(w);

        auto& owned = m_impl->m_owned;
        for (auto it = owned.begin(); it != owned.end(); ++it) {
            if (it->get() == &w) {
                owned.erase(it);
                return;
            }
        }
    }

    Widget* Window::findTopByName(const std::string& name) {
        if (name.empty() || !m_impl) return nullptr;
        if (m_impl->m_emulated) return m_impl->m_emulated->FindWidget<Widget>(name);
        for (Widget* w : m_impl->m_widgets) {
            if (w && w->hasObjectName(name)) return w;
        }
        return nullptr;
    }

    void Window::forEachTopImpl(void (*fn)(Widget&, void*), void* ctx) {
        if (!m_impl || !fn) return;
        if (m_impl->m_emulated) {
            for (Widget* w : m_impl->m_emulated->children_) {
                if (w) fn(*w, ctx);
            }
            return;
        }
        for (Widget* w : m_impl->m_widgets) {
            if (w) fn(*w, ctx);
        }
    }

    void Window::setFocusedWidget(Widget* w) {
        if (m_impl->m_focused == w) return;
        if (m_impl->m_focused) m_impl->m_focused->setFocused(false);
        m_impl->m_focused = w;
        if (w) w->setFocused(true);

        if (IsClosed()) return;
        RequestRedraw();

        if (Supports(Capability::InputOnScreenKeyboard)) {
            if (m_impl->m_native)
                m_impl->m_native->ShowSoftKeyboard(w && w->WantsSoftKeyboard());
        }
    }

    Widget* Window::focusedWidget() const {
        Widget* f = m_impl->m_focused;
        if (!f) return nullptr;
        for (Widget* w = f; w; w = w->parent()) {
            if (!w->parent()) {
                const auto& v = m_impl->m_widgets;
                if (std::find(v.begin(), v.end(), w) != v.end()) return f;
                break;
            }
        }
        m_impl->m_focused = nullptr;
        return nullptr;
    }

    void Window::SetTheme(const Theme& theme) {
        if (!m_impl) return;

        m_impl->m_baseTheme = theme;
        m_impl->m_userTheme = true;

        m_impl->m_dark = Themes::Luminance(theme.bg) < 0.5f;
        m_impl->applyTheme(theme);

        RequestRedraw();

        applyTitleBarDarkMode();

        for (Widget* w : m_impl->m_widgets)
            if (w) applyThemeToAll(*w);

        m_impl->propagateThemeToDependents();
        m_impl->frameAfterInput();
    }

    const Theme& Window::theme() const {
        static const Theme kFallback = Themes::Light();
        return m_impl ? m_impl->m_theme : kFallback;
    }

    bool Window::SetThemeByName(const std::string& name) {
        if (!Themes::Has(name)) return false;
        SetTheme(Themes::Get(name));
        return true;
    }

    void Window::applyThemeToAll(Widget& w) {
        w.ApplyTheme(m_impl->m_theme);
        for (Widget* c : w.children())
            if (c) applyThemeToAll(*c);
    }

    void Window::applyThemeToPristine(Widget& w) {
        if (!w.hasCustomStyle())
            w.ApplyTheme(m_impl->m_theme);
        for (Widget* c : w.children())
            if (c) applyThemeToPristine(*c);
    }

    bool Window::SystemPrefersDarkMode() { return Plat::SystemPrefersDark(); }

    bool Window::isFollowingSystemDarkMode() const { return m_impl && m_impl->m_followSystem; }
    bool Window::isDarkMode() const { return m_impl && m_impl->m_dark; }

    void* Window::hwnd() const { return m_impl ? m_impl->m_nativeHandle : nullptr; }
    bool  Window::isWindowActive() const {
        return m_impl && m_impl->m_native && m_impl->m_native->IsActive();
    }

    void Window::reportImeSpot(float x, float lineTop, float lineBottom) {
        if (!m_impl || !m_impl->m_native) return;
        m_impl->m_native->SetImeSpot(x, lineTop, lineBottom);
    }

    std::string Window::GetClipboardText() const {
        if (!m_impl || !m_impl->m_native) return std::string();
        return m_impl->m_native->GetClipboardText();
    }

    void Window::SetClipboardText(const std::string& utf8) {
        if (!m_impl || !m_impl->m_native) return;
        m_impl->m_native->SetClipboardText(utf8);
    }

    void Window::applyTitleBarDarkMode() {
        if (!m_impl) return;
        m_impl->m_native->SetDarkTitleBar(m_impl->m_dark);
    }

    void Window::refreshDarkModeFromSystem() {
        if (!m_impl) return;
        if (!m_impl->m_followSystem) return;

        const bool dark = Plat::SystemPrefersDark();
        m_impl->applyDarkMode(dark);
        m_impl->frameAfterInput();
    }

    void Window::SetFollowSystemDarkMode(bool follow) {
        if (!m_impl) return;
        m_impl->m_followSystem = follow;
        if (!follow) return;

        m_impl->applyDarkMode(Plat::SystemPrefersDark(), true);
        m_impl->frameAfterInput();
    }

    void Window::SetDarkMode(bool dark) {
        if (!m_impl) return;

        m_impl->m_followSystem = false;
        m_impl->applyDarkMode(dark, true);
        m_impl->frameAfterInput();
    }

    Window::Window()
        :m_impl(std::make_unique<Impl>()) {
        m_impl->m_ownerWindow = this;

        m_impl->m_native = std::unique_ptr<Plat::IWindow>(Plat::CreateNativeWindow());

        Plat::WindowDesc desc;
        desc.title = "";
        desc.centered = true;

        if (!m_impl->m_native->Create(desc, *m_impl)) {
            return;
        }

        m_impl->m_nativeHandle = m_impl->m_native->NativeHandle();

        m_impl->m_fontRenderer.SetScale(m_impl->m_native->RenderScale());

        m_impl->m_fullscreen = m_impl->m_native->StartsImmersive();

        m_impl->m_countsAsLive = true;
        ++LiveWindowCount();

        m_impl->m_countsAsAnchor = true;
        ++AnchorWindowCount();

        m_impl->m_dark = m_impl->m_followSystem ? Plat::SystemPrefersDark() : m_impl->m_dark;
        m_impl->applyTheme(Themes::ForDarkMode(m_impl->m_dark));

    }

    Window::Window(const GadgetDesc& desc)
        : m_impl(std::make_unique<Impl>()) {
        m_impl->m_ownerWindow = this;

#if ZEKIT_HAS_COMPOSITION_API
        m_impl->m_api = Rhi::Api::D3D12;
#endif
        m_impl->m_clearAlpha = 0.0f;

        m_impl->m_native = std::unique_ptr<Plat::IWindow>(Plat::CreateNativeWindow());

        Plat::WindowDesc wd;
        wd.title = "";
        wd.width  = std::max(1, desc.width);
        wd.height = std::max(1, desc.height);
        wd.frameless = true;
        wd.perPixelTransparent = true;
        wd.centered = false;
        wd.x = desc.x;
        wd.y = desc.y;

        if (!m_impl->m_native->Create(wd, *m_impl)) return;

        m_impl->m_nativeHandle = m_impl->m_native->NativeHandle();
        m_impl->m_gadget = true;
        m_impl->m_fontRenderer.SetScale(m_impl->m_native->RenderScale());
        m_impl->m_fullscreen = false;

        if (!desc.topmost) m_impl->m_native->SetTopmost(false);

        m_impl->m_countsAsLive = true;
        ++LiveWindowCount();

        if (desc.keepsAppAlive) {
            m_impl->m_countsAsAnchor = true;
            ++AnchorWindowCount();
        }

        m_impl->m_dark = m_impl->m_followSystem ? Plat::SystemPrefersDark() : m_impl->m_dark;
        m_impl->applyTheme(Themes::ForDarkMode(m_impl->m_dark));
    }

    bool Window::IsGadget() const {
        return m_impl && m_impl->m_gadget;
    }

    bool Window::IsPerPixelTransparent() const {
        if (!m_impl || !m_impl->m_renderBackend) return false;
        return m_impl->m_renderBackend->IsPerPixelTransparent();
    }

    Window::Window(Window& parent, int x, int y, int width, int height, ChildKind kind,
                   bool borderlessAtBirth)
        : Window(parent, x, y, width, height, kind, borderlessAtBirth,
                 true) {}

    Window::Window(Window& parent, int x, int y, int width, int height, ChildKind kind,
                   bool borderlessAtBirth, bool systemShadow)
        : m_impl(std::make_unique<Impl>()) {
        m_impl->m_ownerWindow = this;
        m_impl->m_isChild = true;
        m_impl->m_parentImpl = parent.m_impl.get();

        const bool overlayAtBirth = (kind == ChildKind::Overlay);
        if (overlayAtBirth) m_impl->m_clearAlpha = 0.0f;

        m_impl->m_parentImpl->registerDependent(m_impl.get());

        const bool autoPos = (x == kAutoGeometry && y == kAutoGeometry);
        ResolveChildGeometry(parent, x, y, width, height);

        const bool forceSoft = [] {
            const char* v = std::getenv("ZEKIT_FORCE_SOFT_SUBWINDOW");
            return v && *v != '0' && *v != '\0';
        }();
        const bool nativeChildOk = !forceSoft
            && parent.m_impl->m_native != nullptr
            && Has(parent.m_impl->m_native->Capabilities(), Capability::ChildWindow)
            && parent.m_impl->NativeHandle() != nullptr;

        if (!nativeChildOk) {
            m_impl->createEmulatedChild(parent, x, y, width, height);
            if (autoPos) CenterInHost();
            return;
        }

        void* parentHandle = parent.m_impl->NativeHandle();
        m_impl->m_native = std::unique_ptr<Plat::IWindow>(Plat::CreateNativeWindow());

        const bool owned = (kind == ChildKind::Owned);
        const bool overlay = (kind == ChildKind::Overlay);
        const bool popup = (kind == ChildKind::Popup) || overlay;
        const bool freed = (kind == ChildKind::Free);

        Plat::WindowDesc desc;
        desc.parent    = parentHandle;
        desc.x         = x;
        desc.y         = y;
        desc.width     = width;
        desc.height    = height;
        desc.centered  = false;
        desc.relation  = popup ? Plat::Relation::Popup
                       : owned ? Plat::Relation::Owned
                       : freed ? Plat::Relation::Free
                               : Plat::Relation::Child;

#if ZEKIT_HAS_COMPOSITION_API
        desc.perPixelTransparent = overlay && (m_impl->m_api == Rhi::Api::D3D12);
#else
        desc.perPixelTransparent = overlay || popup;
#endif
        desc.systemShadow = systemShadow;

        if (!m_impl->m_native->Create(desc, *m_impl)) {
            return;
        }

        m_impl->m_nativeHandle = m_impl->m_native->NativeHandle();

        m_impl->m_fontRenderer.SetScale(m_impl->m_native->RenderScale());

        m_impl->m_fontRenderer.Init();

        m_impl->m_fullscreen = false;

        if (borderlessAtBirth && (owned || freed)) {
            m_impl->m_native->SetBorderless(true);
            SetSize(width, height);
        }

        if (autoPos) CenterInHost();

        if (owned || popup) m_impl->m_native->Show();

        m_impl->m_userHidden = false;

        m_impl->m_followSystem = false;
        m_impl->m_dark = parent.m_impl->m_dark;
        m_impl->m_userTheme = false;
        m_impl->applyTheme(parent.m_impl->m_theme);

        m_impl->m_api = parent.m_impl->m_api;
        m_impl->m_gpu = parent.m_impl->m_gpu;

    }

    Window::Window(Window& parent, int x, int y, ChildKind kind)
        : Window(parent, x, y, 0, 0, kind) {}

    Window::Window(Window& parent, ChildKind kind)
        : Window(parent, kAutoGeometry, kAutoGeometry, 0, 0, kind) {}

    bool Window::IsChildWindow() const {
        return m_impl && m_impl->m_isChild;
    }

    void Window::SetInheritOnParentClose(bool on) {
        if (!m_impl) return;
        m_impl->m_inheritOnParentClose = on;
    }

    bool Window::inheritOnParentClose() const {
        return m_impl && m_impl->m_inheritOnParentClose;
    }

    bool Window::IsInherited() const {
        return m_impl && m_impl->m_inherited;
    }

    void Window::SetOnInherited(std::function<void(Window&)> cb) {
        if (!m_impl) return;
        m_impl->m_onInherited = std::move(cb);
    }

    Window& Window::CreateChildWindow(int x, int y, int width, int height,
                                      ChildKind kind, bool borderlessAtBirth) {
        auto child = std::make_unique<Window>(*this, x, y, width, height, kind,
                                              borderlessAtBirth);
        Window& ref = *child;
        m_impl->m_childWindows.push_back(std::move(child));
        return ref;
    }

    Window& Window::CreateChildWindow(int x, int y, ChildKind kind) {
        return CreateChildWindow(x, y, 0, 0, kind);
    }

    Window& Window::CreateChildWindow(ChildKind kind) {
        return CreateChildWindow(kAutoGeometry, kAutoGeometry, 0, 0, kind);
    }

    void Window::DestroyChildWindow(Window& child) {
        if (!m_impl) return;
        auto& table = m_impl->m_childWindows;
        for (auto it = table.begin(); it != table.end(); ++it) {
            if (it->get() == &child) {
                table.erase(it);
                return;
            }
        }
    }

    int Window::DestroyClosedChildWindows() {
        if (!m_impl) return 0;
        auto& table = m_impl->m_childWindows;
        int n = 0;
        for (auto it = table.begin(); it != table.end(); ) {
            if ((*it) && (*it)->IsClosed()) {
                it = table.erase(it);
                ++n;
            } else {
                ++it;
            }
        }
        return n;
    }

    int Window::childWindowCount() const {
        return m_impl ? static_cast<int>(m_impl->m_childWindows.size()) : 0;
    }

    void Window::Show() {
        if (IsClosed()) return;

        m_impl->m_userHidden = false;

        if (m_impl->m_emulated) { m_impl->m_emulated->Open(); return; }

        const bool trace = Plat::TraceLifecycle();
        const auto tShow = std::chrono::steady_clock::now();
        const auto msSince = [](std::chrono::steady_clock::time_point t) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t).count();
            };
        m_impl->m_fontRenderer.Init();
        if (trace) std::cerr << "[Zekit/trace] ① 字体面（FreeType） " << msSince(tShow) << "ms\n";

        if (!m_impl->m_renderEnabled) {
            m_impl->m_native->Show();
            if (m_impl->m_isChild) RequestRedraw();
            if (trace) std::cerr << "[Zekit/trace] 渲染权已交给外部：跳过后端创建\n";
            return;
        }

        m_impl->preheatBackend();

        const bool preHeat = m_impl->m_native->CanPrepareRendererBeforeShow();

        bool backendReady = false;
        if (preHeat) backendReady = m_impl->ensureBackend();

        const auto tMap = std::chrono::steady_clock::now();
        m_impl->m_native->Show();
        if (trace) std::cerr << "[Zekit/trace] ③ 上屏（平台层） " << msSince(tMap) << "ms\n";

        if (!preHeat) backendReady = m_impl->ensureBackend();

        const auto tFrame = std::chrono::steady_clock::now();
        if (backendReady) m_impl->RenderFrame();
        if (trace)
            std::cerr << "[Zekit/trace] ⑥ 首帧 " << msSince(tFrame) << "ms"
                      << "（Show 全程 " << msSince(tShow) << "ms）\n";

        if (m_impl->m_isChild) RequestRedraw();
    }

    void Window::Hide() {
        if (m_impl) m_impl->m_userHidden = true;

        if (m_impl->m_emulated) { m_impl->m_emulated->SetVisible(false); return; }
        if (m_impl->m_native) m_impl->m_native->Hide();
    }

    void Window::Close() {
        if (m_impl->m_emulated) { m_impl->m_emulated->Close(); return; }
        if (m_impl->m_native) m_impl->m_native->PostClose();
    }

    bool Window::IsClosed() const {
        if (!m_impl) return true;

        if (m_impl->m_emulated) {
            if (m_impl->m_emulatedClosed) return true;
            if (m_impl->m_parentImpl && m_impl->m_parentImpl->NativeClosed()) return true;
            return false;
        }

        if (m_impl->m_native && m_impl->m_native->IsClosed()) return true;
        return m_impl->m_nativeHandle == nullptr;
    }

    void Window::GetClientSize(int& w, int& h) const {
        w = 0; h = 0;
        if (IsClosed()) return;
        if (m_impl->m_emulated) {
            w = static_cast<int>(m_impl->m_emulated->width());
            h = static_cast<int>(m_impl->m_emulated->height());
            return;
        }
        m_impl->m_native->GetClientSize(w, h);
    }

    int Window::width() const  { int w = 0, h = 0; GetClientSize(w, h); return w; }
    int Window::height() const { int w = 0, h = 0; GetClientSize(w, h); return h; }

    void Window::SetSize(int w, int h) {
        if (IsClosed()) return;
        w = std::max(w, 1);
        h = std::max(h, 1);
        if (m_impl->m_minW > 0) w = std::max(w, m_impl->m_minW);
        if (m_impl->m_minH > 0) h = std::max(h, m_impl->m_minH);
        if (m_impl->m_maxW > 0) w = std::min(w, m_impl->m_maxW);
        if (m_impl->m_maxH > 0) h = std::min(h, m_impl->m_maxH);

        if (m_impl->m_emulated) {
            m_impl->m_emulated->SetWindowSize(static_cast<float>(w), static_cast<float>(h));
            return;
        }
        m_impl->m_native->SetClientSize(w, h);
    }

    void Window::SetCenterOnResize(bool on) {
        if (IsClosed()) return;
        m_impl->m_centerOnResize = on;
        m_impl->m_native->SetCenterOnResize(on);
    }

    bool Window::centerOnResize() const {
        return m_impl && m_impl->m_centerOnResize;
    }

    void Window::SetPosition(int x, int y) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) {
            m_impl->m_emulated->SetWindowPosition(static_cast<float>(x), static_cast<float>(y));
            return;
        }
        m_impl->m_native->SetPosition(x, y);
    }

    void Window::adjustSize() {
        if (IsClosed()) return;
        if (!m_impl) return;

        float needW = 0.0f, needH = 0.0f;
        for (Widget* w : m_impl->m_widgets) {
            if (!w || !w->isVisible()) continue;
            if (w->fillWindow()) continue;

            const float cw = w->prefWidth()  > 0.0f ? w->prefWidth()  : w->hintWidth();
            const float ch = w->prefHeight() > 0.0f ? w->prefHeight() : w->hintHeight();

            needW = std::max(needW, w->x() + cw + w->margin().left + w->margin().right);
            needH = std::max(needH, w->y() + ch + w->margin().top + w->margin().bottom);
        }

        if (needW <= 0.0f || needH <= 0.0f) return;

        SetSize(static_cast<int>(std::ceil(needW)), static_cast<int>(std::ceil(needH)));
    }

    TextMetrics Window::MeasureText(std::string_view utf8, const FontDesc& font) const {
        if (!m_impl) return TextMetrics{};
        return m_impl->m_fontRenderer.MeasureText(utf8, font);
    }

    void Window::CenterOnScreen() {
        if (IsClosed()) return;
        if (m_impl->m_isChild) { CenterInHost(); return; }
        if (m_impl->m_emulated) return;
        m_impl->m_native->CenterOnScreen();
    }

    void Window::CenterInHost() {
        if (IsClosed()) return;

        Window* host = (m_impl->m_parentImpl) ? m_impl->m_parentImpl->m_ownerWindow
                                              : nullptr;
        if (!host) { CenterOnScreen(); return; }

        int hostW = 0, hostH = 0;
        host->GetClientSize(hostW, hostH);
        if (hostW <= 0 || hostH <= 0) return;

        int w = 0, h = 0;
        GetClientSize(w, h);

        int l = 0, t = 0, r = 0, b = 0;
        if (m_impl->m_native) m_impl->m_native->GetFrameInsets(l, t, r, b);
        const int outerW = w + l + r;
        const int outerH = h + t + b;

        SetPositionRelativeToHost((hostW - outerW) / 2, (hostH - outerH) / 2);
    }

    bool Window::GetScreenWorkArea(int& x, int& y, int& w, int& h) const {
        if (!m_impl || IsClosed()) return false;
        if (m_impl->m_emulated) return false;
        if (!m_impl->m_native) return false;
        return m_impl->m_native->GetScreenWorkArea(x, y, w, h);
    }

    namespace Screen {

        namespace {
            void CopyFrom(const Plat::DisplayGeometry& src, Display& dst) {
                dst.x      = src.x;
                dst.y      = src.y;
                dst.width  = src.width;
                dst.height = src.height;

                dst.workX      = src.workX;
                dst.workY      = src.workY;
                dst.workWidth  = src.workWidth;
                dst.workHeight = src.workHeight;

                dst.primary = src.primary;
            }
        }

        bool Primary(Display& out) {
            Plat::DisplayGeometry g;
            if (!Plat::QueryPrimaryDisplay(g)) return false;
            CopyFrom(g, out);
            return true;
        }

        int Count() {
            return List(nullptr, 0);
        }

        int List(Display* out, int maxCount) {
            if (out && maxCount > 0) {
                std::vector<Plat::DisplayGeometry> buf(static_cast<std::size_t>(maxCount));
                const int n = Plat::QueryDisplays(buf.data(), maxCount);
                const int write = (n < maxCount) ? n : maxCount;
                for (int i = 0; i < write; ++i) CopyFrom(buf[static_cast<std::size_t>(i)], out[i]);
                return n;
            }
            return Plat::QueryDisplays(nullptr, 0);
        }

        bool PrimaryWorkArea(int& x, int& y, int& w, int& h) {
            Display d;
            if (!Primary(d)) return false;
            x = d.workX;
            y = d.workY;
            w = d.workWidth;
            h = d.workHeight;
            return w > 0 && h > 0;
        }

    }

    void Window::GetClientOrigin(int& x, int& y) const {
        x = 0; y = 0;
        if (!m_impl || IsClosed()) return;
        if (m_impl->m_emulated) return;
        if (!m_impl->m_native) return;
        m_impl->m_native->ClientOriginToScreen(x, y);
    }

    void Window::SetTitle(const std::string& title) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) { m_impl->m_emulated->SetTitle(title); return; }
        m_impl->m_native->SetTitle(title);
    }

    int Window::GetDpi() const {
        if (IsClosed()) return 0;
        if (m_impl->m_emulated) return 0;
        return m_impl->m_native->Dpi();
    }

    float Window::GetScaleFactor() const {
        if (IsClosed()) return 1.0f;
        if (m_impl->m_emulated) return 1.0f;
        return m_impl->m_native->ScaleFactor();
    }

    void Window::RequestRedraw() {
        if (IsClosed()) return;
        if (Widget* pw = m_impl->m_paintingWidget.load(std::memory_order_relaxed))
            PaintCacheAccess::MarkDirty(*pw);
        else
            m_impl->m_allPaintDirty.store(true, std::memory_order_release);
        requestFrameOnly();
    }

    void Window::SetSwapInterval(int mode) {
        m_impl->m_swapIntervalMode = (mode >= 0) ? ((mode == 0) ? 0 : 1) : -1;
    }

    void Window::requestFrameOnly(const Widget* from) {
        if (IsClosed()) return;
        if (m_impl->m_geometrySync) {
            if (Plat::TraceRedraw())
                std::cerr << "[Zekit/redraw] 同步期请求（已忽略）："
                << (from ? typeid(*from).name() : "app/platform") << "\n";
            return;
        }
        m_impl->m_frameRequests.fetch_add(1, std::memory_order_relaxed);
        if (m_impl->m_inFrame && Plat::TraceRedraw()
            && m_impl->m_frameRequestLogs < 200) {
            ++m_impl->m_frameRequestLogs;
            std::cerr << "[Zekit/redraw] 帧内请求 " << m_impl->m_frameRequests.load()
                << "（阶段 " << m_impl->m_framePhase << "）："
                << (from ? typeid(*from).name() : "app/platform");
            if (from)
                std::cerr << " @ " << from->x() << "," << from->y()
                    << " " << from->width() << "x" << from->height();
            std::cerr << "\n";
        }
        m_impl->m_redrawPending.store(true, std::memory_order_release);
        m_impl->m_native->RequestRedraw();
    }

    void Window::invalidateAllPaint() {
        if (IsClosed()) return;
        m_impl->markAllPaintDirty();
    }

    void Window::RequestFrameWake(int afterMs) {
        if (IsClosed()) return;
        if (Widget* pw = m_impl->m_paintingWidget.load(std::memory_order_relaxed))
            PaintCacheAccess::SetTimed(*pw, true);
        if (m_impl->m_driveMode == Window::DriveMode::Immediate) return;

        const auto due = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(afterMs > 0 ? afterMs : 0);
        if (!m_impl->m_wakePending || due < m_impl->m_wakeAt) m_impl->m_wakeAt = due;
        m_impl->m_wakePending = true;

        m_impl->m_native->RequestWakeup(afterMs);
    }

    void Window::CancelFrameWake() {
        if (IsClosed()) return;
        m_impl->m_wakePending = false;
        m_impl->m_native->CancelWakeup();
    }

    Window::TimerId Window::SetInterval(int intervalMs, std::function<void()> fn) {
        if (!m_impl || IsClosed() || !fn) return 0;
        if (Window* root = m_impl->timerRoot(); root && root != this)
            return root->SetInterval(intervalMs, std::move(fn));
        if (intervalMs < 1) intervalMs = 1;

        const TimerId id = m_impl->m_nextTimerId++;
        Impl::TimerTask task;
        task.id = id;
        task.intervalMs = intervalMs;
        task.repeat = true;
        task.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
        task.fn = std::move(fn);
        m_impl->m_timers.push_back(std::move(task));

        if (m_impl->m_native) m_impl->m_native->RequestWakeup(intervalMs);
        return id;
    }

    Window::TimerId Window::SetTimeout(int delayMs, std::function<void()> fn) {
        if (!m_impl || IsClosed() || !fn) return 0;
        if (Window* root = m_impl->timerRoot(); root && root != this)
            return root->SetTimeout(delayMs, std::move(fn));
        if (delayMs < 0) delayMs = 0;

        const TimerId id = m_impl->m_nextTimerId++;
        Impl::TimerTask task;
        task.id = id;
        task.intervalMs = delayMs;
        task.repeat = false;
        task.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        task.fn = std::move(fn);
        m_impl->m_timers.push_back(std::move(task));

        if (m_impl->m_native) m_impl->m_native->RequestWakeup(delayMs);
        return id;
    }

    void Window::ClearTimer(TimerId id) {
        if (!m_impl || id == 0) return;
        if (Window* root = m_impl->timerRoot(); root && root != this)
            return root->ClearTimer(id);
        auto& timers = m_impl->m_timers;
        timers.erase(std::remove_if(timers.begin(), timers.end(),
            [id](const Impl::TimerTask& t) { return t.id == id; }), timers.end());
    }

    void Window::Post(std::function<void()> fn) {
        if (!fn || !m_impl) return;
        {
            std::lock_guard<std::mutex> lock(m_impl->m_postMutex);
            m_impl->m_posts.push_back(std::move(fn));
        }
        if (m_impl->m_native) m_impl->m_native->PostWake();
    }

    void Window::SetAcceptFileDrop(bool on) {
        if (!m_impl || IsClosed()) return;
        if (m_impl->m_acceptFileDrop == on) return;
        m_impl->m_acceptFileDrop = on;
        if (m_impl->m_native) m_impl->m_native->SetAcceptFileDrop(on);
    }

    bool Window::acceptsFileDrop() const {
        return m_impl && m_impl->m_acceptFileDrop;
    }

    void Window::SetOnFileDrop(std::function<void(const std::vector<std::string>&)> cb) {
        if (!m_impl) return;
        m_impl->m_onFileDrop = std::move(cb);
        if (m_impl->m_onFileDrop && !m_impl->m_acceptFileDrop) SetAcceptFileDrop(true);
    }

    bool Window::ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out) {
        out.clear();
        if (!m_impl || IsClosed()) return false;
        if (!Supports(Capability::NativeFileDialog)) {
            std::cerr << "[Zekit] ShowFileDialog：本平台没有原生文件对话框"
                         "（Capability::NativeFileDialog 为假）。"
                         "可以用 Kit::FileDropZone / 自己实现选择界面替代。\n";
            return false;
        }
        return m_impl->m_native->ShowFileDialog(desc, out);
    }

    void Window::Minimize() {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->Minimize();
    }

    void Window::Maximize() {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        if (m_impl->m_fullscreen) SetFullscreen(false);
        if (m_impl->m_native->IsMinimized()) Restore();
        m_impl->m_native->Maximize();
    }

    void Window::Restore() {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        if (m_impl->m_fullscreen) { SetFullscreen(false); return; }
        m_impl->m_native->Restore();
    }

    void Window::SetTopmost(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetTopmost(on);
    }

    void Window::SetRenderEnabled(bool on) {
        if (!m_impl) return;
        const bool was = m_impl->m_renderEnabled;
        m_impl->m_renderEnabled = on;

        if (!on && was && m_impl->m_renderBackend) {
            m_impl->m_renderBackend->DetachTextureSinks(&m_impl->m_fontRenderer,
                                                        &m_impl->m_imageStore);
            m_impl->m_renderBackend->Shutdown();
            m_impl->m_renderBackend.reset();
            if (Plat::TraceLifecycle())
                std::cerr << "[Zekit/trace] 渲染权已交出：Kit 后端已拆（HWND 归外部渲染器）\n";
        }

        if (on && m_impl->m_native && !m_impl->m_native->IsClosed() && !m_impl->m_emulated)
            RequestRedraw();
    }

    bool Window::isRenderEnabled() const {
        return m_impl ? m_impl->m_renderEnabled : false;
    }

    void Window::SetMinimumSize(int w, int h) {
        if (IsClosed()) return;
        m_impl->m_minW = std::max(w, 0);
        m_impl->m_minH = std::max(h, 0);
        m_impl->m_native->SetSizeLimits(m_impl->m_minW, m_impl->m_minH,
                                        m_impl->m_maxW, m_impl->m_maxH);
        if (m_impl->m_minW > 0 || m_impl->m_minH > 0) {
            int cw, ch; GetClientSize(cw, ch);
            const int nw = m_impl->m_minW > 0 ? std::max(cw, m_impl->m_minW) : cw;
            const int nh = m_impl->m_minH > 0 ? std::max(ch, m_impl->m_minH) : ch;
            if (nw != cw || nh != ch) SetSize(nw, nh);
        }
    }

    void Window::SetMaximumSize(int w, int h) {
        if (IsClosed()) return;
        m_impl->m_maxW = std::max(w, 0);
        m_impl->m_maxH = std::max(h, 0);
        m_impl->m_native->SetSizeLimits(m_impl->m_minW, m_impl->m_minH,
                                        m_impl->m_maxW, m_impl->m_maxH);
        if (m_impl->m_maxW > 0 || m_impl->m_maxH > 0) {
            int cw, ch; GetClientSize(cw, ch);
            const int nw = m_impl->m_maxW > 0 ? std::min(cw, m_impl->m_maxW) : cw;
            const int nh = m_impl->m_maxH > 0 ? std::min(ch, m_impl->m_maxH) : ch;
            if (nw != cw || nh != ch) SetSize(nw, nh);
        }
    }

    void Window::SetResizable(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetResizable(on);
    }

    void Window::applyTitleBarStyle() {
        if (IsClosed()) return;
        m_impl->m_native->ApplyTitleBar(
            m_impl->m_titleBarVisible,
            m_impl->m_minBtnEnabled,
            m_impl->m_maxBtnEnabled,
            m_impl->m_closeBtnEnabled);
    }

    void Window::SetTitleBarVisible(bool visible) {
        if (IsClosed()) return;
        if (m_impl->m_titleBarVisible == visible) return;
        m_impl->m_titleBarVisible = visible;
        applyTitleBarStyle();
    }

    void Window::SetMinimizeButtonEnabled(bool enabled) {
        if (IsClosed()) return;
        if (m_impl->m_minBtnEnabled == enabled) return;
        m_impl->m_minBtnEnabled = enabled;
        applyTitleBarStyle();
    }

    void Window::SetMaximizeButtonEnabled(bool enabled) {
        if (IsClosed()) return;
        if (m_impl->m_maxBtnEnabled == enabled) return;
        m_impl->m_maxBtnEnabled = enabled;
        applyTitleBarStyle();
    }

    void Window::SetCloseButtonEnabled(bool enabled) {
        if (IsClosed()) return;
        if (m_impl->m_closeBtnEnabled == enabled) return;
        m_impl->m_closeBtnEnabled = enabled;
        applyTitleBarStyle();
    }

    bool Window::MinimizeButtonEnabled() const {
        return m_impl->m_minBtnEnabled;
    }

    bool Window::MaximizeButtonEnabled() const {
        return m_impl->m_maxBtnEnabled;
    }

    bool Window::CloseButtonEnabled() const {
        return m_impl->m_closeBtnEnabled;
    }

    void Window::SetEnabled(bool enabled) {
        if (IsClosed()) return;
        if (m_impl->m_enabled == enabled) return;
        m_impl->m_enabled = enabled;
        if (m_impl->m_emulated) return;
        if (m_impl->m_native) m_impl->m_native->SetEnabled(enabled);
    }

    bool Window::isEnabled() const {
        return m_impl ? m_impl->m_enabled : true;
    }

    void Window::SetOnClosed(std::function<void(Window&)> cb) {
        m_impl->m_onClosed = std::move(cb);
    }

    void Window::SetOnCloseRequest(std::function<bool(Window&)> cb) {
        m_impl->m_onCloseRequest = std::move(cb);
    }

    std::function<bool(Window&)> Window::OnCloseRequest() const {
        return m_impl->m_onCloseRequest;
    }

    void Window::StartWindowDrag() const {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->StartWindowDrag();
    }

    void Window::SetIcon(void* hIcon) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetIconFromHandle(hIcon);
    }

    void Window::SetWindowIcon(const std::string& path) {
        if (IsClosed() || path.empty()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetIconFromFile(path);
    }

    void Window::SetWindowIconFromResource(int resId) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetIconFromResource(resId);
    }

    void Window::SetOpacity(float alpha) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetOpacity(alpha);
    }

    void Window::SetBorderless(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetBorderless(on);
    }

    void Window::SetBorderlessResizable(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetBorderlessResizable(on);
    }

    bool Window::IsBorderlessResizable() const {
        if (IsClosed()) return false;
        if (m_impl->m_emulated) return false;
        return m_impl->m_native->IsBorderlessResizable();
    }

    void Window::SetRoundedCorners(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetRoundedCorners(on);
    }

    bool Window::IsRoundedCorners() const {
        if (IsClosed()) return false;
        if (m_impl->m_emulated) return false;
        return m_impl->m_native->IsRoundedCorners();
    }

    void Window::SetClickThrough(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        m_impl->m_native->SetClickThrough(on);
    }

    void Window::SetPositionRelativeToHost(int x, int y) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) {
            m_impl->m_emulated->SetWindowPosition(static_cast<float>(x), static_cast<float>(y));
            return;
        }
        m_impl->m_native->SetPositionRelativeToHost(x, y);
    }

    void Window::SetFullscreen(bool on) {
        if (IsClosed()) return;
        if (m_impl->m_emulated) return;
        if (m_impl->m_fullscreen == on) return;
        m_impl->m_native->SetFullscreen(on);
        m_impl->m_fullscreen = on;
        if (!on) applyTitleBarStyle();
    }

    bool Window::IsFullscreen() const {
        return m_impl->m_fullscreen;
    }

    void Window::GetMousePosition(int& x, int& y) const {
        if (IsClosed()) { x = -1; y = -1; return; }
        x = m_impl->m_lastMouseX;
        y = m_impl->m_lastMouseY;
    }

    bool Window::IsMouseButtonDown(MouseButton b) const {
        if (IsClosed()) return false;
        return m_impl->m_native->IsMouseButtonDown(static_cast<Plat::MouseButton>(
            static_cast<uint32_t>(b)));
    }

    Capability Window::capabilities() const {
        if (!m_impl->m_native) return Capability::None;
        return m_impl->m_native->Capabilities();
    }

    bool Window::Supports(Capability c) const {
        return HasAll(capabilities(), c);
    }

    void Window::ShowSoftKeyboard(bool on) {
        if (IsClosed()) return;
        m_impl->m_native->ShowSoftKeyboard(on);
    }

    bool Window::IsSoftKeyboardVisible() const {
        if (IsClosed()) return false;
        return m_impl->m_native->IsSoftKeyboardVisible();
    }

    float Window::softKeyboardHeight() const {
        return m_impl->m_softKeyboardHeight;
    }

    void Window::GetSafeAreaInsets(float& left, float& top,
        float& right, float& bottom) const {
        if (IsClosed()) { left = top = right = bottom = 0.0f; return; }
        m_impl->m_native->GetSafeAreaInsets(left, top, right, bottom);
    }

    void Window::SetOnAppLifecycle(std::function<void(AppLifecycle)> fn) {
        m_impl->m_lifecycleFn = std::move(fn);
    }

    void Window::SetOnSafeAreaChanged(
        std::function<void(float, float, float, float)> fn) {
        m_impl->m_safeAreaFn = std::move(fn);
    }

    void Window::SetDriveMode(DriveMode mode) {
        m_impl->m_driveMode = mode;
    }

    Window::DriveMode Window::driveMode() const {
        return m_impl->m_driveMode;
    }

    void Window::SetFrameRate(int fps) {
        m_impl->m_frameRate = std::max(1, fps);
    }

    int Window::frameRate() const {
        return m_impl->m_frameRate;
    }

    bool Window::UseRenderer(Rhi::Api api) {
        if (!m_impl) return false;
        if (!m_impl->m_nativeHandle) return false;

        if (m_impl->m_renderBackend &&
            m_impl->m_renderBackend->api() == api &&
            m_impl->m_renderBackend->IsReady()) {
            m_impl->m_api = api;
            return true;
        }

        if (m_impl->m_renderBackend) {
            m_impl->m_renderBackend->DetachTextureSinks(&m_impl->m_fontRenderer,
                                                        &m_impl->m_imageStore);
            m_impl->m_renderBackend->Shutdown();
            m_impl->m_renderBackend.reset();
        }

        m_impl->m_api = api;
        if (!m_impl->ensureBackend()) {
            m_impl->m_api = Rhi::Api::Software;
            std::cerr << "[Zekit] switching renderer to " << Rhi::ApiName(api)
                      << " failed." << std::endl;
            return false;
        }

        const bool fellBack = (m_impl->m_renderBackend->api() != api);
        return !fellBack;
    }

    Rhi::Api Window::rendererApi() const {
        if (!m_impl) return Rhi::Api::OpenGL;
        if (m_impl->m_renderBackend) return m_impl->m_renderBackend->api();
        return m_impl->m_api;
    }

    bool Window::UseExternalRenderer(std::shared_ptr<Kit::ICanvasRenderer> renderer) {
        if (!m_impl) return false;
        if (!m_impl->m_nativeHandle) return false;

        if (!renderer) {
            if (m_impl->m_renderBackend) {
                m_impl->m_renderBackend->DetachTextureSinks(&m_impl->m_fontRenderer,
                                                            &m_impl->m_imageStore);
                m_impl->m_renderBackend->Shutdown();
                m_impl->m_renderBackend.reset();
            }
            m_impl->m_externalRenderer.reset();
            m_impl->m_api = Rhi::DefaultApi();
            return true;
        }

        if (m_impl->m_externalRenderer == renderer) return true;

        if (m_impl->m_renderBackend) {
            std::cerr << "[Zekit] UseExternalRenderer: 渲染后端已初始化，"
                         "必须在第一次绘制之前调用（需要重建窗口才能换）。"
                      << std::endl;
            return false;
        }

        m_impl->m_externalRenderer = std::move(renderer);
        m_impl->m_api = Rhi::Api::External;
        return true;
    }

    bool Window::UseGpu(Rhi::Gpu gpu) {
        if (!m_impl) return false;
        if (!m_impl->m_nativeHandle) return false;

        if (m_impl->m_gpu == gpu) return true;
        m_impl->m_gpu = gpu;

        if (!m_impl->m_renderBackend) return true;

        if (m_impl->m_renderBackend->IsReady()) {
            m_impl->m_renderBackend->DetachTextureSinks(&m_impl->m_fontRenderer,
                                                        &m_impl->m_imageStore);
        }
        m_impl->m_renderBackend->Shutdown();
        m_impl->m_renderBackend.reset();
        return true;
    }

    Rhi::Gpu Window::gpuPreference() const {
        if (!m_impl) return Rhi::DefaultGpu();
        return m_impl->m_gpu;
    }

    void Window::SetClearAlpha(float alpha) {
        if (!m_impl) return;
        m_impl->m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
        if (m_impl->m_renderBackend)
            m_impl->m_renderBackend->SetClearAlpha(m_impl->m_clearAlpha);
    }

    Rhi::DeviceInfo Window::deviceInfo() const {
        if (!m_impl || !m_impl->m_renderBackend) return {};
        return m_impl->m_renderBackend->deviceInfo();
    }

    int Window::exec() {
        if (m_impl->m_isChild) return 0;

        if (!m_impl->m_native || m_impl->m_native->IsClosed())
            return -1;

        m_impl->m_quit = false;

        struct ExecLoopGuard {
            int& count;
            ExecLoopGuard() : count(ExecLoopCount()) { ++count; }
            ~ExecLoopGuard() { --count; }
        } execGuard;

        struct ExecHostGuard {
            explicit ExecHostGuard(Impl* self) { Impl::ExecHosts().push_back(self); }
            ~ExecHostGuard() { Impl::ExecHosts().pop_back(); }
        } hostGuard(m_impl.get());

        const bool traceInit = Plat::TraceLifecycle();
        const auto t0 = std::chrono::steady_clock::now();
        const auto since0 = [&t0] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            };

        m_impl->m_fontRenderer.Init();
        if (traceInit)
            std::cerr << "[Zekit/trace] ① 字体面就绪 +" << since0() << "ms\n";

        if (!m_impl->ensureBackend()) {
            if (m_impl->m_native->RenderBackendPending()) {
                if (Plat::TraceLifecycle())
                    std::cerr << "[Zekit/trace] 渲染后端暂未就绪（等系统交付 surface）"
                                 "—— 正常时序，继续跑主循环\n";
            }
            else {
                std::cerr << "[Zekit] no usable render backend; "
                             "running message-only loop (nothing will be drawn).\n";

                while (!m_impl->m_quit) {
                    if (m_impl->m_native->IsClosed() && LiveWindowCount() == 0) {
                        m_impl->m_quit = true;
                        if (traceInit)
                            std::cerr << "[Zekit/trace] 平台层已收摊（原生窗口已销毁、"
                                         "无存活窗口）→ 纯消息循环收工\n";
                        break;
                    }
                    m_impl->m_native->WaitEvents(-1);
                    while (m_impl->m_native->PollEvent()) {
                        if (m_impl->m_quit) break;
                    }
                }

                m_impl->shutdownRenderer();
                return m_impl->m_native->ExitCode();
            }
        }
        if (traceInit)
            std::cerr << "[Zekit/trace] ② 渲染后端就绪 +" << since0() << "ms（"
            << Rhi::ApiName(m_impl->m_renderBackend->api()) << "）\n";

        const bool eventDriven = (m_impl->m_driveMode == Window::DriveMode::EventDriven);
        const bool immediate = (m_impl->m_driveMode == Window::DriveMode::Immediate);

        struct TimerPrecisionGuard {
            Plat::IWindow* win = nullptr;
            explicit TimerPrecisionGuard(Plat::IWindow* w, bool need) : win(need ? w : nullptr) {
                if (win) win->SetTimerPrecision(true);
            }
            ~TimerPrecisionGuard() { if (win) win->SetTimerPrecision(false); }
        } timerGuard(m_impl->m_native.get(), !eventDriven);

        if (!immediate) {
            m_impl->RenderFrame();
            if (traceInit)
                std::cerr << "[Zekit/trace] ③ 首帧已提交 +" << since0() << "ms\n";
        }

        m_impl->resetPacing();
        while (!m_impl->m_quit) {
            if (!m_impl->pumpOnce(-1)) break;
        }

        m_impl->shutdownRenderer();

        return m_impl->m_native->ExitCode();
    }

    bool Window::PollEvents(int waitMs) {
        if (!m_impl) return false;
        if (m_impl->m_quit) return false;

        if (!m_impl->m_native || m_impl->m_native->IsClosed()) return false;

        m_impl->ensurePump();

        if (waitMs < -1) waitMs = -1;

        if (waitMs > 0) m_impl->acquireTimerPrecision();

        const bool alive = m_impl->pumpOnce(waitMs);

        if (IsClosed()) return false;
        return alive;
    }

    int Window::RunRealtime(const std::function<void(double)>& onFrame, int fps) {
        if (!onFrame) return 0;

        if (!PollEvents(0)) return 0;
        if (fps < 0) fps = 0;

        if (fps > 0) m_impl->acquireTimerPrecision();

        const auto period = std::chrono::microseconds(
            fps > 0 ? static_cast<long long>(1000000.0 / fps) : 0);
        auto prev = std::chrono::steady_clock::now();

        auto nextAt = prev;

        while (!ShouldQuit()) {
            const auto frameStart = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(frameStart - prev).count();
            prev = frameStart;

            onFrame(dt);

            PollEvents(0);
            if (ShouldQuit()) break;
            if (fps <= 0) continue;

            nextAt += period;
            const auto now = std::chrono::steady_clock::now();
            if (now < nextAt) {
                std::this_thread::sleep_for(nextAt - now);
            }
            else if (now - nextAt >= period) {
                nextAt = now;
            }
        }
        return 0;
    }

    bool Window::ShouldQuit() const {
        if (!m_impl) return true;
        if (m_impl->m_quit) return true;
        return IsClosed();
    }

    void Window::RenderNow() {
        if (!m_impl) return;
        if (IsClosed()) return;

        if (!m_impl->m_renderEnabled) return;

        if (!m_impl->ensurePump()) return;
        m_impl->RenderFrame();
    }

    Window::~Window() {
        Impl::UnregisterHeir(m_impl.get());
        m_impl->m_onInherited = nullptr;
        if (m_impl->m_parentImpl)
            m_impl->m_parentImpl->unregisterDependent(m_impl.get());

        if (m_impl)
            m_impl->shutdownRenderer();

        if (m_impl) m_impl->m_childWindows.clear();

        if (m_impl->m_emulated) {
            m_impl->m_emulated->SetOnClosed(nullptr);
            if (m_impl->m_parentImpl && m_impl->m_parentImpl->m_ownerWindow)
                m_impl->m_parentImpl->m_ownerWindow->RemoveWidget(*m_impl->m_emulated);
            m_impl->m_emulated = nullptr;
        }

        for (Widget* w : m_impl->m_widgets) w->setOwner(nullptr);
        m_impl->m_widgets.clear();

        OverlayHost::Forget(*this);

        if (m_impl->m_native) {
            m_impl->m_native->Destroy();
            m_impl->m_native.reset();
        }

        m_impl->m_hover = nullptr;
        m_impl->m_pressed = nullptr;
        m_impl->m_ownerWindow = nullptr;

        for (const Impl::FilterSlot& slot : m_impl->m_filters) {
            if (slot.filter) slot.filter->forgetInstall(this);
        }
        m_impl->m_filters.clear();
    }
}
