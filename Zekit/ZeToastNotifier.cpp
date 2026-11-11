
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <cstring>
#include <vector>
#if defined(_MSC_VER)
#pragma comment(lib, "gdiplus.lib")
#endif

namespace gp = Gdiplus;
#endif

#include "include/ZeToastNotifier.h"
#include "include/Zekit.h"
#include "ZeOverlayHost.h"

namespace Kit {

    namespace {
        constexpr int kInitialW = 360;
        constexpr int kInitialH = 120;

        ToastPlace ToHostPlace(NotifyAnchor a) {
            switch (a) {
            case NotifyAnchor::TopLeft:     return ToastPlace::TopLeft;
            case NotifyAnchor::TopCenter:   return ToastPlace::TopCenter;
            case NotifyAnchor::TopRight:    return ToastPlace::TopRight;
            case NotifyAnchor::BottomLeft:  return ToastPlace::BottomLeft;
            case NotifyAnchor::BottomRight: return ToastPlace::BottomRight;
            case NotifyAnchor::Custom:
            default:
                return ToastPlace::BottomRight;
            }
        }

        std::vector<ToastNotifier*>& LiveNotifiers() {
            static std::vector<ToastNotifier*> v;
            return v;
        }

        void TrackNotifier(ToastNotifier* n) {
            auto& v = LiveNotifiers();
            if (std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n);
        }

        void UntrackNotifier(ToastNotifier* n) {
            auto& v = LiveNotifiers();
            v.erase(std::remove(v.begin(), v.end(), n), v.end());
        }
    }

#if defined(_WIN32)
    struct ToastEngineImpl {
        static constexpr float kIconD        = 22.0f;
        static constexpr float kIconGap      = 10.0f;
        static constexpr float kPadL         = 16.0f;
        static constexpr float kPadR         = 16.0f;
        static constexpr float kPadTop       = 14.0f;
        static constexpr float kPadBottom    = 16.0f;
        static constexpr float kCloseSize    = 22.0f;
        static constexpr float kCloseTopGap  = 10.0f;
        static constexpr float kShadow       = 22.0f;
        static constexpr float kRadius       = 10.0f;
        static constexpr int   kTimerId      = 1;
        static constexpr int   kTimerMs      = 16;
        static constexpr int   kStackGap     = 10;
        static constexpr UINT  kAppShow      = WM_APP + 0x5441;
        static constexpr UINT  kAppClear     = WM_APP + 0x5442;
        static constexpr UINT  kAppCloseAll  = WM_APP + 0x5443;
        static constexpr const wchar_t* kFontFace  = L"Microsoft YaHei UI";
        static constexpr const wchar_t* kCardClass = L"ZekitToastCard";
        static constexpr const wchar_t* kMgrClass  = L"ZekitToastMgr";

        struct Card {
            ToastEngineImpl* eng = nullptr;
            HWND  hwnd = nullptr;
            std::wstring text;
            ToastLevel level = ToastLevel::Info;
            int   durationMs = 2200;

            int   W = 0, H = 0;
            float cw = 0, ch = 0;
            float cx0 = 0, cy0 = 0;
            float textL = 0, textR = 0;
            float textTop = 0;
            float textH = 0;
            float iconCx = 0, iconCy = 0;
            RECT  closeRc{};
            bool  closeHover = false;
            int   fontPx = 0;
            gp::Font* font = nullptr;

            float fx = 0, fy = 0;
            float tx = 0, ty = 0;
            float alpha = 0.0f;
            DWORD born = 0, lastTick = 0, shownMs = 0;
            DWORD closeStart = 0;
            bool  closing = false;
            bool  hoverWin = false;
            unsigned long long seq = 0;

            HDC     memDC = nullptr;
            HBITMAP memBmp = nullptr;
            void*   bits = nullptr;
            gp::Bitmap* shadowBmp = nullptr;

            ~Card() { CleanupFrame(); delete font; }
            void CleanupFrame() {
                if (memDC)  { DeleteDC(memDC);   memDC = nullptr; }
                if (memBmp) { DeleteObject(memBmp); memBmp = nullptr; }
                bits = nullptr;
                delete shadowBmp; shadowBmp = nullptr;
            }
        };

        struct ShowReq { std::string text; ToastLevel level; int durationMs; };

        explicit ToastEngineImpl(ToastNotifier& owner) : o(owner) {
            ApplyThemeSnap(owner.host_.theme());
        }
        ~ToastEngineImpl() {
            for (Card* c : cards) {
                if (c->hwnd) { KillTimer(c->hwnd, kTimerId); DestroyWindow(c->hwnd); }
                delete c;
            }
            cards.clear();
            if (mgr) { DestroyWindow(mgr); mgr = nullptr; }
        }

        void ApplyThemeSnap(const Theme& t) {
            if (t.surface.a > 0.01f) cardBg = t.surface;
            if (t.text.a   > 0.01f)  textCol = t.text;
            if (t.border.a > 0.01f)  borderCol = t.border;
            accent[0] = (t.toast.accentInfo.a    > 0.01f) ? t.toast.accentInfo    : t.primary;
            accent[1] = (t.toast.accentSuccess.a > 0.01f) ? t.toast.accentSuccess : t.success;
            accent[2] = (t.toast.accentWarning.a > 0.01f) ? t.toast.accentWarning : t.warning;
            accent[3] = (t.toast.accentError.a   > 0.01f) ? t.toast.accentError   : t.danger;
            if (t.toast.closeGlyph.a   > 0.01f) closeGlyph   = t.toast.closeGlyph;
            if (t.toast.closeHoverBg.a > 0.01f) closeHoverBg = t.toast.closeHoverBg;
            if (t.toast.text.font.pixelSize > 0) fontSize = t.toast.text.font.pixelSize;
            RelayoutAll();
        }

        Color4f EffBg()     const { return o.bgSet_ ? o.bg_ : cardBg; }
        Color4f EffFg()     const { return o.fgSet_ ? o.fg_ : textCol; }
        float   EffFontPx() const { return (o.fontSize_ > 0) ? float(o.fontSize_) : float(fontSize); }

        void Show(std::string text, ToastLevel level, int durationMs) {
            if (!Ensure()) return;
            if (GetCurrentThreadId() != threadId) {
                ShowReq* r = new ShowReq{ std::move(text), level, durationMs };
                if (!PostMessageW(mgr, kAppShow, 0, reinterpret_cast<LPARAM>(r)))
                    delete r;
                return;
            }
            ShowNow(std::move(text), level, durationMs);
        }

        void ShowNow(std::string text, ToastLevel level, int durationMs) {
            if (text.empty()) return;
            SoundFor(level);
            Card* c = new Card();
            c->eng = this;
            c->text = Utf8ToWide(text);
            c->level = level;
            c->durationMs = durationMs;
            c->born = c->lastTick = GetTickCount();
            c->seq = ++seqGen;
            const HWND h = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                kCardClass, L"", WS_POPUP,
                0, 0, 200, 100,
                nullptr, nullptr, GetModuleHandleW(nullptr), c);
            if (!h) { delete c; return; }
            cards.push_back(c);
            ApplyFlagsTo(c);
            Evict();
            PlaceAll();
            const bool right = (o.anchor_ == NotifyAnchor::TopRight ||
                                o.anchor_ == NotifyAnchor::BottomRight);
            c->fx = c->tx + (right ? 46.0f : -46.0f);
            c->fy = c->ty;
            ShowWindow(h, SW_SHOWNOACTIVATE);
            TickCard(c);
        }

        void ClearAll() {
            if (GetCurrentThreadId() != threadId && mgr) {
                PostMessageW(mgr, kAppClear, 0, 0);
                return;
            }
            for (Card* c : cards) BeginClose(c);
        }

        std::size_t Count() const { return cards.size(); }
        bool        IsShowing() const { return !cards.empty(); }

        void ReapplyPlacement() { PlaceAll(); }
        void ReapplyLayout()    { RelayoutAll(); }
        void RepaintAll() {
            for (Card* c : cards) InvalidateRect(c->hwnd, nullptr, FALSE);
        }

        void Evict() {
            if (o.maxVisible_ <= 0) return;
            while (cards.size() > static_cast<std::size_t>(o.maxVisible_)) {
                Card* oldest = nullptr;
                for (Card* c : cards)
                    if (!c->closing && (!oldest || c->seq < oldest->seq)) oldest = c;
                if (!oldest) break;
                BeginClose(oldest);
            }
        }

        void ApplyWindowFlags() {
            for (Card* c : cards) ApplyFlagsTo(c);
        }

    private:
        ToastNotifier& o;
        std::vector<Card*> cards;
        unsigned long long seqGen = 0;
        DWORD threadId = 0;
        HWND  mgr = nullptr;

        Color4f cardBg{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f textCol{ 0.12f, 0.14f, 0.16f, 1.0f };
        Color4f borderCol = Color4f::FromHex(0xE5E6EB);
        Color4f closeGlyph = Color4f::FromHex(0x9AA4B2);
        Color4f closeHoverBg{ 0.0f, 0.0f, 0.0f, 0.08f };
        Color4f accent[4] = {
            Color4f::FromHex(0x2563EB), Color4f::FromHex(0x16A34A),
            Color4f::FromHex(0xD97706), Color4f::FromHex(0xDC2626) };
        unsigned fontSize = 14;

        static int LevelIndex(ToastLevel l) {
            switch (l) {
            case ToastLevel::Success: return 1;
            case ToastLevel::Warning: return 2;
            case ToastLevel::Error:   return 3;
            case ToastLevel::Info:
            default:                  return 0;
            }
        }

        static void EnsureGdiplus() {
            static bool ok = false;
            if (ok) return;
            gp::GdiplusStartupInput in;
            in.GdiplusVersion = 1;
            ULONG_PTR token = 0;
            ok = (GdiplusStartup(&token, &in, nullptr) == gp::Ok);
        }

        bool Ensure() {
            if (mgr) return true;
            EnsureGdiplus();
            RegisterClasses();
            threadId = GetCurrentThreadId();
            mgr = CreateWindowExW(WS_EX_TOOLWINDOW, kMgrClass, L"", WS_POPUP,
                0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
            return mgr != nullptr;
        }

        static void RegisterClasses() {
            static bool done = false;
            if (done) return;
            const HINSTANCE hInst = GetModuleHandleW(nullptr);
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = hInst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpfnWndProc = CardWndProc;
            wc.lpszClassName = kCardClass;
            RegisterClassExW(&wc);
            WNDCLASSEXW mc{};
            mc.cbSize = sizeof(mc);
            mc.hInstance = hInst;
            mc.lpfnWndProc = MgrWndProc;
            mc.lpszClassName = kMgrClass;
            RegisterClassExW(&mc);
            done = true;
        }

        static inline BYTE F2B(float v) {
            return static_cast<BYTE>(std::lround(v * 255.0f));
        }
        static inline gp::Color ToGp(const Color4f& c) {
            return gp::Color(F2B(c.a), F2B(c.r), F2B(c.g), F2B(c.b));
        }
        static std::wstring Utf8ToWide(const std::string& s) {
            if (s.empty()) return L"";
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                static_cast<int>(s.size()), w.data(), n);
            return w;
        }
        static HDC MeasureDC() {
            static HDC s_dc = [] {
                HDC dc = CreateCompatibleDC(nullptr);
                HBITMAP bmp = CreateCompatibleBitmap(dc, 1, 1);
                SelectObject(dc, bmp);
                DeleteObject(bmp);
                return dc;
            }();
            return s_dc;
        }
        static gp::Font* MakeFont(float px) {
            return new gp::Font(kFontFace, px, gp::FontStyleRegular, gp::UnitPixel);
        }
        static float MeasureTextWidth(gp::Font* f, const std::wstring& s) {
            if (s.empty() || !f) return 0.0f;
            gp::Graphics g(MeasureDC());
            gp::RectF out;
            g.MeasureString(s.c_str(), static_cast<INT>(s.size()), f, gp::PointF(0, 0), &out);
            return out.Width;
        }
        static float MeasureWrapHeight(gp::Font* f, const std::wstring& s, float width) {
            if (s.empty() || !f) return 0.0f;
            gp::Graphics g(MeasureDC());
            gp::StringFormat sf;
            sf.SetAlignment(gp::StringAlignmentNear);
            sf.SetLineAlignment(gp::StringAlignmentNear);
            gp::RectF layout(0, 0, std::max(width, 8.0f), 20000.0f);
            gp::RectF bound;
            g.MeasureString(s.c_str(), static_cast<INT>(s.size()), f, layout, &sf, &bound,
                nullptr, nullptr);
            return bound.Height;
        }
        static void AddRoundRect(gp::GraphicsPath& p, const gp::RectF& rc, float r) {
            if (r < 0.5f) { p.AddRectangle(rc); return; }
            const float d = r * 2.0f;
            p.StartFigure();
            p.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
            p.AddArc(rc.GetRight() - d, rc.Y, d, d, 270.0f, 90.0f);
            p.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0.0f, 90.0f);
            p.AddArc(rc.X, rc.GetBottom() - d, d, d, 90.0f, 90.0f);
            p.CloseFigure();
        }

        float CardWidth(const std::wstring& text) const {
            float mw = o.maxWidth_;
            if (mw <= 0.5f) {
                gp::Font f(kFontFace, EffFontPx(), gp::FontStyleRegular, gp::UnitPixel);
                mw = 0.0f;
                size_t start = 0;
                while (start <= text.size()) {
                    const size_t nl = text.find(L'\n', start);
                    const std::wstring line = text.substr(start,
                        (nl == std::wstring::npos ? text.size() : nl) - start);
                    mw = std::max(mw, MeasureTextWidth(&f, line));
                    if (nl == std::wstring::npos) break;
                    start = nl + 1;
                }
                mw += kPadL + kPadR + kIconD + kIconGap + kCloseSize + 10.0f;
            }
            return std::clamp(mw, 240.0f, 640.0f);
        }

        void LayoutCard(Card* c) {
            c->cx0 = c->cy0 = kShadow;
            c->cw = CardWidth(c->text);
            const float iconSpan = kIconD + kIconGap;
            c->textL = c->cx0 + kPadL + iconSpan;
            c->closeRc = {
                static_cast<LONG>(c->cx0 + c->cw - kPadR - kCloseSize),
                static_cast<LONG>(c->cy0 + kCloseTopGap),
                static_cast<LONG>(c->cx0 + c->cw - kPadR),
                static_cast<LONG>(c->cy0 + kCloseTopGap + kCloseSize) };
            c->textR = static_cast<float>(c->closeRc.left) - 10.0f;

            const float textW = std::max(c->textR - c->textL, 40.0f);
            float msgH = MeasureWrapHeight(c->font, c->text, textW);
            if (msgH < 18.0f) msgH = 18.0f;
            c->textH = std::max(msgH, kIconD);
            c->iconCx = c->cx0 + kPadL + kIconD * 0.5f;
            c->iconCy = c->cy0 + kPadTop + (c->textH - kIconD) * 0.5f + kIconD * 0.5f;
            c->textTop = c->cy0 + kPadTop;
            c->ch = kPadTop + c->textH + kPadBottom;
            c->W = static_cast<int>(std::lround(c->cw + kShadow * 2.0f));
            c->H = static_cast<int>(std::lround(c->ch + kShadow * 2.0f));
            BuildFrameBuffers(c);
        }

        void BuildFrameBuffers(Card* c) {
            c->CleanupFrame();
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = c->W;
            bi.bmiHeader.biHeight = -c->H;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            const HDC sdc = GetDC(nullptr);
            c->memDC = CreateCompatibleDC(sdc);
            c->memBmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &c->bits, nullptr, 0);
            ReleaseDC(nullptr, sdc);
            SelectObject(c->memDC, c->memBmp);

            c->shadowBmp = new gp::Bitmap(c->W, c->H, PixelFormat32bppARGB);
            gp::Graphics gs(c->shadowBmp);
            gs.SetSmoothingMode(gp::SmoothingModeAntiAlias);
            gs.Clear(gp::Color(0, 0, 0, 0));
            gp::SolidBrush br(gp::Color(5, 8, 10, 14));
            const int N = 16;
            for (int k = N - 1; k >= 0; --k) {
                const float f = static_cast<float>(k) / static_cast<float>(N - 1);
                const float inflate = 0.8f + kShadow * f;
                gp::RectF rc(c->cx0 - inflate, c->cy0 - inflate,
                    c->cw + inflate * 2.0f, c->ch + inflate * 2.0f);
                gp::GraphicsPath gp_;
                AddRoundRect(gp_, rc, kRadius + inflate * 0.35f);
                gs.FillPath(&br, &gp_);
            }
        }

        void DrawGlyph(gp::Graphics& g, const Card* c) const {
            const float cx = c->iconCx, cy = c->iconCy, D = kIconD;
            gp::SolidBrush bg(ToGp(accent[LevelIndex(c->level)]));
            g.FillEllipse(&bg, cx - D * 0.5f, cy - D * 0.5f, D, D);
            const float u = D / 26.0f;
            gp::SolidBrush dot(gp::Color(255, 255, 255, 255));
            gp::Pen glyph(gp::Color(255, 255, 255, 255), 2.1f * u);
            glyph.SetStartCap(gp::LineCapRound);
            glyph.SetEndCap(gp::LineCapRound);
            glyph.SetLineJoin(gp::LineJoinRound);
            switch (c->level) {
            case ToastLevel::Success: {
                gp::PointF pts[3] = {
                    gp::PointF(cx - 5.0f * u, cy + 0.5f * u),
                    gp::PointF(cx - 1.5f * u, cy + 4.0f * u),
                    gp::PointF(cx + 5.5f * u, cy - 4.0f * u) };
                g.DrawLines(&glyph, pts, 3);
                break;
            }
            case ToastLevel::Error: {
                g.DrawLine(&glyph, cx - 4.0f * u, cy - 4.0f * u, cx + 4.0f * u, cy + 4.0f * u);
                g.DrawLine(&glyph, cx + 4.0f * u, cy - 4.0f * u, cx - 4.0f * u, cy + 4.0f * u);
                break;
            }
            case ToastLevel::Warning: {
                g.DrawLine(&glyph, cx, cy - 6.5f * u, cx, cy + 0.5f * u);
                g.FillEllipse(&dot, cx - 1.8f * u, cy + 3.4f * u, 3.6f * u, 3.6f * u);
                break;
            }
            case ToastLevel::Info:
            default:
                g.DrawLine(&glyph, cx, cy - 1.0f * u, cx, cy + 5.5f * u);
                g.FillEllipse(&dot, cx - 1.6f * u, cy - 6.6f * u, 3.2f * u, 3.2f * u);
                break;
            }
        }

        void PaintCard(Card* c) {
            if (!c->memDC || !c->bits) return;
            memset(c->bits, 0, static_cast<size_t>(c->W) * static_cast<size_t>(c->H) * 4);

            gp::Graphics g(c->memDC);
            g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(gp::PixelOffsetModeHalf);

            if (c->shadowBmp) g.DrawImage(c->shadowBmp, 0, 0, c->W, c->H);

            {
                gp::RectF rc(c->cx0 + 0.5f, c->cy0 + 0.5f, c->cw - 1.0f, c->ch - 1.0f);
                gp::GraphicsPath path;
                AddRoundRect(path, rc, kRadius);
                gp::SolidBrush bg(ToGp(EffBg()));
                g.FillPath(&bg, &path);
                gp::Pen pen(ToGp(borderCol), 1.0f);
                g.DrawPath(&pen, &path);
            }

            DrawGlyph(g, c);

            if (!c->text.empty()) {
                const float availH = std::max((c->cy0 + c->ch) - c->textTop - 2.0f, 8.0f);
                gp::RectF r(c->textL, c->textTop, c->textR - c->textL, availH);
                gp::StringFormat sf;
                sf.SetAlignment(gp::StringAlignmentNear);
                sf.SetLineAlignment(gp::StringAlignmentNear);
                sf.SetFormatFlags(gp::StringFormatFlagsNoClip);
                gp::SolidBrush br(ToGp(EffFg()));
                g.DrawString(c->text.c_str(), static_cast<INT>(c->text.size()),
                    c->font, r, &sf, &br);
            }

            {
                const RECT& cr = c->closeRc;
                if (c->closeHover) {
                    gp::SolidBrush hb(ToGp(closeHoverBg));
                    g.FillEllipse(&hb, static_cast<gp::REAL>(cr.left),
                        static_cast<gp::REAL>(cr.top),
                        static_cast<gp::REAL>(cr.right - cr.left),
                        static_cast<gp::REAL>(cr.bottom - cr.top));
                }
                const float cx = (cr.left + cr.right) * 0.5f;
                const float cy = (cr.top + cr.bottom) * 0.5f;
                gp::Pen xp(ToGp(c->closeHover ? EffFg() : closeGlyph), 1.4f);
                xp.SetStartCap(gp::LineCapRound);
                xp.SetEndCap(gp::LineCapRound);
                const float a = 3.4f;
                g.DrawLine(&xp, cx - a, cy - a, cx + a, cy + a);
                g.DrawLine(&xp, cx + a, cy - a, cx - a, cy + a);
            }

            const float ga = std::clamp(c->alpha * std::clamp(o.opacity_, 0.0f, 1.0f),
                0.0f, 1.0f);
            BYTE* p = static_cast<BYTE*>(c->bits);
            const int n = c->W * c->H;
            if (ga >= 0.002f) {
                for (int i = 0; i < n; ++i, p += 4) {
                    const unsigned a = static_cast<unsigned>(p[3] * ga + 0.5f);
                    if (!a) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
                    p[3] = static_cast<BYTE>(a);
                    p[0] = static_cast<BYTE>((p[0] * a + 127) / 255);
                    p[1] = static_cast<BYTE>((p[1] * a + 127) / 255);
                    p[2] = static_cast<BYTE>((p[2] * a + 127) / 255);
                }
            }

            POINT dst{ static_cast<LONG>(std::lround(c->fx)),
                       static_cast<LONG>(std::lround(c->fy)) };
            SIZE sz{ c->W, c->H };
            POINT src{ 0, 0 };
            BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            const HDC scr = GetDC(nullptr);
            UpdateLayeredWindow(c->hwnd, scr, &dst, &sz, c->memDC, &src, 0, &bf, ULW_ALPHA);
            ReleaseDC(nullptr, scr);
        }

        bool WorkArea(RECT* wa) const {
            POINT p{ 0, 0 };
            if (o.anchor_ == NotifyAnchor::Custom)
                p = { static_cast<LONG>(std::lround(o.customX_)),
                      static_cast<LONG>(std::lround(o.customY_)) };
            const HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (mon && GetMonitorInfoW(mon, &mi)) { *wa = mi.rcWork; return true; }
            return SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0) != 0;
        }

        void PlaceAll() {
            if (cards.empty()) return;
            RECT wa{};
            if (!WorkArea(&wa)) return;

            if (cards.size() > 1)
                std::sort(cards.begin(), cards.end(),
                    [](const Card* a, const Card* b) { return a->seq > b->seq; });

            const int m = static_cast<int>(std::lround(o.margin_));
            const int gap = static_cast<int>(std::lround(o.itemGap_));
            const bool custom = (o.anchor_ == NotifyAnchor::Custom);
            const bool right = (o.anchor_ == NotifyAnchor::TopRight ||
                                o.anchor_ == NotifyAnchor::BottomRight);
            const bool bottom = (o.anchor_ == NotifyAnchor::BottomRight ||
                                 o.anchor_ == NotifyAnchor::BottomLeft);

            float y = custom ? o.customY_
                             : (bottom ? static_cast<float>(wa.bottom - m)
                                       : static_cast<float>(wa.top + m));
            for (Card* c : cards) {
                c->tx = custom ? o.customX_
                               : (right ? static_cast<float>(wa.right - m - c->W)
                                        : static_cast<float>(wa.left + m));
                if (custom || !bottom) { c->ty = y;         y = c->ty + c->H + gap; }
                else                   { c->ty = y - c->H;  y = c->ty - gap; }
            }

            if (custom) return;
            const float limitMin = static_cast<float>(wa.top + m);
            const float limitMax = static_cast<float>(wa.bottom - m);
            for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
                Card* c = *it;
                if (c->closing) continue;
                if (bottom  && c->ty < limitMin - 1.0f) BeginClose(c);
                if (!bottom && c->ty + c->H > limitMax + 1.0f) BeginClose(c);
            }
        }

        void BeginClose(Card* c) {
            if (c && !c->closing) {
                c->closing = true;
                c->closeStart = GetTickCount();
            }
        }

        void DestroyCard(Card* c) {
            if (c->hwnd) {
                KillTimer(c->hwnd, kTimerId);
                DestroyWindow(c->hwnd);
                c->hwnd = nullptr;
            }
            const auto it = std::find(cards.begin(), cards.end(), c);
            if (it != cards.end()) cards.erase(it);
            delete c;
            PlaceAll();
        }

        void TickCard(Card* c) {
            if (!c->hwnd) return;
            const DWORD now = GetTickCount();
            const float fadeS = static_cast<float>(std::clamp(o.fadeMs_, 60, 500));

            if (!c->closing) {
                if (o.pauseOnHover_ && c->hoverWin) {
                    c->lastTick = now;
                }
                else {
                    c->shownMs += now - c->lastTick;
                    c->lastTick = now;
                }
                const DWORD el = now - c->born;
                const float in = std::min(1.0f, el / fadeS);
                c->alpha = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
                if (c->durationMs > 0 && c->shownMs >= static_cast<DWORD>(c->durationMs))
                    BeginClose(c);
            }
            else {
                const DWORD el = now - c->closeStart;
                const float out = std::min(1.0f, el / fadeS);
                c->alpha = (1.0f - out) * (1.0f - out);
                if (out >= 1.0f) { DestroyCard(c); return; }
            }

            auto lerp = [](float cur, float target) {
                if (std::fabs(target - cur) < 0.6f) return target;
                return cur + (target - cur) * 0.18f;
            };
            c->fx = lerp(c->fx, c->tx);
            c->fy = lerp(c->fy, c->ty);

            InvalidateRect(c->hwnd, nullptr, FALSE);
        }

        void ApplyFlagsTo(Card* c) const {
            if (!c->hwnd) return;
            const LONG_PTR ex = GetWindowLongPtrW(c->hwnd, GWL_EXSTYLE);
            const LONG_PTR want = o.clickThrough_
                ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
            if (want != ex) SetWindowLongPtrW(c->hwnd, GWL_EXSTYLE, want);
            SetWindowPos(c->hwnd, o.topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }

        void RelayoutAll() {
            for (Card* c : cards) {
                const int px = static_cast<int>(EffFontPx());
                if (px != c->fontPx) {
                    c->fontPx = px;
                    delete c->font;
                    c->font = MakeFont(static_cast<float>(px));
                }
                LayoutCard(c);
                InvalidateRect(c->hwnd, nullptr, FALSE);
            }
            PlaceAll();
        }

        void SoundFor(ToastLevel level) {
            switch (o.sound_) {
            case ToastSound::Off:
                return;
            case ToastSound::Custom: {
                if (!o.soundFile_.empty()) {
                    const std::wstring w = Utf8ToWide(o.soundFile_);
                    if (PlaySoundW(w.c_str(), nullptr,
                            SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
                        return;
                }
                if (o.customSound_) o.customSound_();
                return;
            }
            case ToastSound::System:
            default: {
                UINT id = MB_ICONASTERISK;
                if (level == ToastLevel::Error)        id = MB_ICONHAND;
                else if (level == ToastLevel::Warning) id = MB_ICONEXCLAMATION;
                MessageBeep(id);
                return;
            }
            }
        }

        static bool InClose(const Card* c, int x, int y) {
            const RECT& rc = c->closeRc;
            const int h = 3;
            return x >= rc.left - h && x <= rc.right + h &&
                   y >= rc.top - h && y <= rc.bottom + h;
        }

        static LRESULT CALLBACK CardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            Card* c = reinterpret_cast<Card*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg) {
            case WM_CREATE: {
                c = reinterpret_cast<Card*>(
                    reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
                c->hwnd = hwnd;
                c->fontPx = static_cast<int>(c->eng->EffFontPx());
                c->font = MakeFont(static_cast<float>(c->fontPx));
                c->eng->LayoutCard(c);
                SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
                return 0;
            }
            case WM_TIMER:
                if (c) c->eng->TickCard(c);
                return 0;

            case WM_PAINT:
                if (c) c->eng->PaintCard(c);
                ValidateRect(hwnd, nullptr);
                return 0;

            case WM_ERASEBKGND:
                return 1;

            case WM_MOUSELEAVE:
                if (c) {
                    c->hoverWin = false;
                    c->closeHover = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;

            case WM_MOUSEMOVE: {
                if (!c) return 0;
                c->hoverWin = true;
                if (!c->closing) {
                    TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                    const bool ch = InClose(c, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                    if (ch != c->closeHover) {
                        c->closeHover = ch;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            }

            case WM_LBUTTONDOWN: {
                if (!c || c->closing) return 0;
                const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
                if (InClose(c, mx, my)) { c->eng->BeginClose(c); return 0; }
                if (mx >= static_cast<LONG>(c->cx0) &&
                    mx <= static_cast<LONG>(c->cx0 + c->cw) &&
                    my >= static_cast<LONG>(c->cy0) &&
                    my <= static_cast<LONG>(c->cy0 + c->ch))
                    c->eng->BeginClose(c);
                return 0;
            }

            case WM_SETCURSOR: {
                if (!c) break;
                RECT rc{};
                GetWindowRect(hwnd, &rc);
                POINT cur;
                GetCursorPos(&cur);
                if (InClose(c, cur.x - rc.left, cur.y - rc.top)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }

            case WM_DESTROY:
                if (c) KillTimer(hwnd, kTimerId);
                return 0;

            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        static LRESULT CALLBACK MgrWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            ToastEngineImpl* e =
                reinterpret_cast<ToastEngineImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg) {
            case WM_CREATE:
                e = reinterpret_cast<ToastEngineImpl*>(
                    reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(e));
                return 0;
            case kAppShow:
                if (e && lp) {
                    ShowReq* r = reinterpret_cast<ShowReq*>(lp);
                    e->ShowNow(std::move(r->text), r->level, r->durationMs);
                    delete r;
                }
                return 0;
            case kAppClear:
                if (e) e->ClearAll();
                return 0;
            case kAppCloseAll:
                if (e) e->ClearAll();
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    };
#endif

    void ToastNotifier::NotifierToast::Paint(FrameCollector& fc) const {
        Toast::Paint(fc);
        owner_.syncWindow();
    }

    ToastNotifier::ToastNotifier(Window& host) : host_(host) {
#if defined(_WIN32)
        anchor_ = NotifyAnchor::BottomRight;
#else
        anchor_ = NotifyAnchor::TopCenter;
#endif
        TrackNotifier(this);
    }

    ToastNotifier::~ToastNotifier() {
        UntrackNotifier(this);

#if defined(_WIN32)
        if (eng_) {
            delete eng_;
            eng_ = nullptr;
        }
#endif
        if (win_) {
            Window* w = win_;
            win_ = nullptr;
            toast_ = nullptr;
            host_.DestroyChildWindow(*w);
        }
    }

    void ToastNotifier::shutdownWithHost() {
#if defined(_WIN32)
        if (eng_) {
            delete eng_;
            eng_ = nullptr;
        }
#endif
        if (toast_) toast_->Clear();
        suppressSync_ = false;
        visible_ = false;
        lastX_ = lastY_ = -1;
        if (win_ && !win_->IsClosed()) win_->Hide();
    }

    void NotifyToastHostClosing(Window& host) {
        std::vector<ToastNotifier*> hits;
        for (ToastNotifier* n : LiveNotifiers()) {
            if (n && &n->host() == &host) hits.push_back(n);
        }
        for (ToastNotifier* n : hits) n->shutdownWithHost();
    }

    bool ToastNotifier::ensureAttached() {
        if (toast_) return true;

        const bool canPopup = !host_.IsClosed()
            && host_.Supports(Capability::ChildWindow)
            && host_.Supports(Capability::PopupWindow);

        if (canPopup) {
            Window& w = host_.CreateChildWindow(16, 16, kInitialW, kInitialH, ChildKind::Popup);
            if (!w.IsClosed()) {
                win_ = &w;
                suppressSync_ = true;

                w.SetClickThrough(clickThrough_);
                w.SetTopmost(topmost_);
                w.SetClearAlpha(0.0f);
                w.SetTheme(host_.theme());

                NotifierToast& t = w.AddWidget(NotifierToast(*this));
                toast_ = &t;
                t.SetInteractive(true);
                pushOptions();
                if (opacitySet_) w.SetOpacity(opacity_);

                suppressSync_ = false;
                return true;
            }
        }

        if (host_.IsClosed()) return false;
        NotifierToast& t = host_.AddWidget(NotifierToast(*this));
        toast_ = &t;
        toastOwnedByHost_ = true;
        pushOptions();
        return true;
    }

    void ToastNotifier::pushOptions() {
        if (!toast_) return;

        if (toastOwnedByHost_) {
            toast_->SetPlace(ToHostPlace(anchor_));
            toast_->SetMargin(margin_);
        } else {
            toast_->SetPlace(ToastPlace::BottomRight);
            toast_->SetMargin(0.0f);
        }

        toast_->SetMaxVisible(maxVisible_);
        toast_->SetMaxWidth(maxWidth_);
        toast_->SetItemGap(itemGap_);
        toast_->SetFadeDuration(fadeMs_);
        if (bgSet_) toast_->SetBackground(bg_);
        if (fgSet_) toast_->SetTextColor(fg_);
        if (fontSize_ > 0) toast_->SetFontSize(fontSize_);
    }

    bool ToastNotifier::applyGeometry() {
        if (!win_ || !toast_ || win_->IsClosed()) return false;

        float w = 0.0f, h = 0.0f;
        if (!toast_->measureStack(w, h)) return false;
        if (w <= 0.5f || h <= 0.5f) return false;

        const int iw = std::max(1, static_cast<int>(std::ceil(w)));
        const int ih = std::max(1, static_cast<int>(std::ceil(h)));

        int cw = 0, ch = 0;
        win_->GetClientSize(cw, ch);
        if (cw != iw || ch != ih) win_->SetSize(iw, ih);

        const int m = static_cast<int>(std::lround(margin_));
        int ax = 0, ay = 0;

        if (anchor_ == NotifyAnchor::Custom) {
            ax = static_cast<int>(std::lround(customX_));
            ay = static_cast<int>(std::lround(customY_));
        }
        else {
            int wkX = 0, wkY = 0, wkW = 0, wkH = 0;
            const bool haveWork = win_->GetScreenWorkArea(wkX, wkY, wkW, wkH);

            if (haveWork) {
                switch (anchor_) {
                case NotifyAnchor::TopLeft:
                    ax = wkX + m;                ay = wkY + m; break;
                case NotifyAnchor::TopCenter:
                    ax = wkX + (wkW - iw) / 2;   ay = wkY + m; break;
                case NotifyAnchor::TopRight:
                    ax = wkX + wkW - m - iw;     ay = wkY + m; break;
                case NotifyAnchor::BottomLeft:
                    ax = wkX + m;                ay = wkY + wkH - m - ih; break;
                case NotifyAnchor::BottomRight:
                default:
                    ax = wkX + wkW - m - iw;     ay = wkY + wkH - m - ih; break;
                }
            }
            else {
                int hw = 0, hh = 0;
                host_.GetClientSize(hw, hh);
                switch (anchor_) {
                case NotifyAnchor::TopLeft:
                    ax = m;                ay = m; break;
                case NotifyAnchor::TopCenter:
                    ax = (hw - iw) / 2;    ay = m; break;
                case NotifyAnchor::TopRight:
                    ax = hw - m - iw;      ay = m; break;
                case NotifyAnchor::BottomLeft:
                    ax = m;                ay = hh - m - ih; break;
                case NotifyAnchor::BottomRight:
                default:
                    ax = hw - m - iw;      ay = hh - m - ih; break;
                }
                if (ax < 0) ax = 0;
                if (ay < 0) ay = 0;
            }
        }

        if (ax != lastX_ || ay != lastY_) {
            win_->SetPosition(ax, ay);
            lastX_ = ax;
            lastY_ = ay;
        }
        return true;
    }

    void ToastNotifier::syncWindow() {
        if (suppressSync_ || !toast_ || !win_) return;

        if (win_->IsClosed()) {
            win_ = nullptr;
            toast_ = nullptr;
            visible_ = false;
            return;
        }

        if (!applyGeometry()) {
            if (visible_) {
                win_->Hide();
                visible_ = false;
            }
            lastX_ = lastY_ = -1;
            return;
        }

        if (!visible_) {
            visible_ = true;
            win_->Show();
            applyGeometry();
        }

        win_->RequestRedraw();
    }

    void ToastNotifier::Show(std::string text, ToastLevel level, int durationMs) {
#if defined(_WIN32)
        if (!eng_) eng_ = new ToastEngineImpl(*this);
        eng_->Show(std::move(text), level, durationMs);
        return;
#else
        if (!ensureAttached() || !toast_) return;
        toast_->Show(std::move(text), level, durationMs);
        syncWindow();
#endif
    }

    void ToastNotifier::ShowInfo(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Info, durationMs);
    }
    void ToastNotifier::ShowSuccess(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Success, durationMs);
    }
    void ToastNotifier::ShowWarning(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Warning, durationMs);
    }
    void ToastNotifier::ShowError(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Error, durationMs);
    }

    void ToastNotifier::Clear() {
#if defined(_WIN32)
        if (eng_) { eng_->ClearAll(); return; }
#endif
        if (!toast_) return;
        toast_->Clear();
        syncWindow();
    }

    std::size_t ToastNotifier::count() const {
#if defined(_WIN32)
        if (eng_) return eng_->Count();
#endif
        return toast_ ? toast_->count() : 0u;
    }

    bool ToastNotifier::isShowing() const {
#if defined(_WIN32)
        if (eng_) return eng_->IsShowing();
#endif
        return toast_ && toast_->isShowing();
    }

    bool ToastNotifier::isOpen() const {
#if defined(_WIN32)
        if (eng_) return eng_->IsShowing();
#endif
        return visible_;
    }

    void ToastNotifier::SetAnchor(NotifyAnchor a) {
        anchor_ = a;
        lastX_ = lastY_ = -1;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyPlacement(); return; }
#endif
        syncWindow();
    }

    void ToastNotifier::SetPosition(float x, float y) {
        customX_ = x;
        customY_ = y;
        anchor_ = NotifyAnchor::Custom;
        lastX_ = lastY_ = -1;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyPlacement(); return; }
#endif
        syncWindow();
    }

    void ToastNotifier::SetMargin(float m) {
        margin_ = (m > 0.0f) ? m : 0.0f;
        lastX_ = lastY_ = -1;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyPlacement(); return; }
#endif
        pushOptions();
        syncWindow();
    }

    void ToastNotifier::SetMaxVisible(int n) {
        maxVisible_ = n;
#if defined(_WIN32)
        if (eng_) { eng_->Evict(); return; }
#endif
        if (toast_) toast_->SetMaxVisible(n);
        syncWindow();
    }

    void ToastNotifier::SetMaxWidth(float w) {
        maxWidth_ = (w > 0.0f) ? w : 0.0f;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyLayout(); return; }
#endif
        if (toast_) toast_->SetMaxWidth(maxWidth_);
        syncWindow();
    }

    void ToastNotifier::SetItemGap(float g) {
        itemGap_ = (g > 0.0f) ? g : 0.0f;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyPlacement(); return; }
#endif
        if (toast_) toast_->SetItemGap(itemGap_);
        syncWindow();
    }

    void ToastNotifier::SetFadeDuration(int ms) {
        fadeMs_ = (ms < 1) ? 1 : ms;
#if defined(_WIN32)
#endif
        if (toast_) toast_->SetFadeDuration(fadeMs_);
    }

    void ToastNotifier::SetBackground(const Color4f& c) {
        bgSet_ = true;
        bg_ = c;
#if defined(_WIN32)
        if (eng_) { eng_->RepaintAll(); return; }
#endif
        if (toast_) toast_->SetBackground(c);
    }

    void ToastNotifier::SetTextColor(const Color4f& c) {
        fgSet_ = true;
        fg_ = c;
#if defined(_WIN32)
        if (eng_) { eng_->RepaintAll(); return; }
#endif
        if (toast_) toast_->SetTextColor(c);
    }

    void ToastNotifier::SetFontSize(unsigned int pixelSize) {
        fontSize_ = pixelSize;
#if defined(_WIN32)
        if (eng_) { eng_->ReapplyLayout(); return; }
#endif
        if (toast_) toast_->SetFontSize(pixelSize);
        syncWindow();
    }

    void ToastNotifier::SetSound(ToastSound mode) { sound_ = mode; }
    void ToastNotifier::SetSoundFile(const std::string& path) { soundFile_ = path; }
    void ToastNotifier::SetCustomSound(std::function<void()> fn) {
        customSound_ = std::move(fn);
    }
    void ToastNotifier::SetPauseOnHover(bool on) { pauseOnHover_ = on; }

    void ToastNotifier::SetClickThrough(bool on) {
        clickThrough_ = on;
#if defined(_WIN32)
        if (eng_) { eng_->ApplyWindowFlags(); return; }
#endif
        if (win_ && !win_->IsClosed()) win_->SetClickThrough(on);
    }

    void ToastNotifier::SetTopmost(bool on) {
        topmost_ = on;
#if defined(_WIN32)
        if (eng_) { eng_->ApplyWindowFlags(); return; }
#endif
        if (win_ && !win_->IsClosed()) win_->SetTopmost(on);
    }

    void ToastNotifier::SetOpacity(float alpha) {
        opacitySet_ = true;
        opacity_ = std::clamp(alpha, 0.0f, 1.0f);
#if defined(_WIN32)
        if (eng_) { eng_->RepaintAll(); return; }
#endif
        if (win_ && !win_->IsClosed()) win_->SetOpacity(opacity_);
    }

    void ToastNotifier::ApplyTheme(const Theme& t) {
#if defined(_WIN32)
        if (eng_) eng_->ApplyThemeSnap(t);
#endif
        if (win_ && !win_->IsClosed()) win_->SetTheme(t);
        if (toast_) toast_->ApplyTheme(t);
    }

    void ToastNotifier::Close() {
#if defined(_WIN32)
        if (eng_) { eng_->ClearAll(); return; }
#endif
        if (toast_) toast_->Clear();
        if (toastOwnedByHost_ && toast_) toast_->SetVisible(false);
        if (win_ && !win_->IsClosed() && visible_) {
            win_->Hide();
            visible_ = false;
            lastX_ = lastY_ = -1;
        }
    }

}
