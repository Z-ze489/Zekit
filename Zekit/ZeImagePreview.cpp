
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "include/ZeImagePreview.h"
#include "include/ZeOverlayWindow.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeValueAnim.h"
#include "ZeFrameCollector.h"
#include "include/ZeText.h"

namespace Kit {

    namespace {
        constexpr float  kFadeMs = 140.0f;
        constexpr float  kCloseSize = 28.0f;
        constexpr float  kCloseMargin = 10.0f;
        constexpr float  kNavSize = 44.0f;
        constexpr float  kNavMargin = 20.0f;
        constexpr double kDoubleClickMs = 400.0;

        float Clamp(float v, float lo, float hi) {
            if (hi < lo) hi = lo;
            return v < lo ? lo : (v > hi ? hi : v);
        }

        void PaintPanel(FrameCollector& fc, float x, float y, float w, float h,
                        float radius, const Color4f& c) {
            if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f) return;
            drawutil::PaintRoundedRect(fc, x, y, w, h, radius, c);
        }
    }

    ImagePreview::ImagePreview() {
        ApplyTheme(Themes::Light());
        SetFocusable(true);
    }

    void ImagePreview::ApplyTheme(const Theme& t) {
        const ImagePreviewStyle& s = t.imagePreview;

        box_ = s.bar;
        titleStyle_ = s.title;
        infoStyle_ = s.info;

        if (s.scrim.a > 0.0f)           scrim_ = s.scrim;
        if (s.controlGlyph.a > 0.0f)    glyphColor_ = s.controlGlyph;
        if (s.controlHoverBg.a > 0.0f)  glyphHoverBg_ = s.controlHoverBg;

        if (s.topBarHeight > 0.0f)      topBarHeight_ = s.topBarHeight;
        if (s.bottomBarHeight > 0.0f)   bottomBarHeight_ = s.bottomBarHeight;

        requestRepaint();
    }

    const BoxStyle& ImagePreview::themeBox(const Theme& t) const {
        return t.imagePreview.bar;
    }

    ImagePreview::~ImagePreview() = default;
    ImagePreview::ImagePreview(ImagePreview&&) noexcept = default;

    bool ImagePreview::nativePopup() const {
        return overlay_ && overlay_->isVisible();
    }

    void ImagePreview::syncOverlay() const {
        if (!open_) {
            if (overlay_) overlay_->Hide();
            return;
        }

        Window* w = this->window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        const float winW = static_cast<float>(w->width());
        const float winH = static_cast<float>(w->height());
        if (winW <= 0.0f || winH <= 0.0f) return;

        if (!overlay_) {
            auto* self = const_cast<ImagePreview*>(this);
            overlay_ = std::make_unique<OverlayWindow>(*w, OverlayLayer::Panel);
            overlay_->SetOnTakenOver([self] { self->Close(); });
            OverlayContent content;
            content.paint = [this](FrameCollector& fc) { paintContent(fc); };
            content.onPress = [self](MouseEvent& e) { return self->OnMousePress(e); };
            content.onRelease = [self](MouseEvent& e) { return self->OnMouseRelease(e); };
            content.onMove = [self](MouseEvent& e) { return self->OnMouseMove(e); };
            content.onWheel = [self](MouseEvent& e) { return self->OnMouseWheel(e); };
            content.onLeave = [self] { self->OnMouseLeave(); };
            content.onHostPress = [self](float, float) {
                if (self->open_) self->Close();
            };
            overlay_->SetContent(std::move(content));
            overlay_->SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
            overlay_->SetOnHostGeometryChanged([this] { syncOverlay(); });
        }

        const bool sameSize = (winW == overlayW_ && winH == overlayH_);
        overlayW_ = winW; overlayH_ = winH;

        if (overlay_->isVisible() && sameSize) overlay_->Repaint();
        else                                   overlay_->Show(0.0f, 0.0f, winW, winH);
    }

    void ImagePreview::Show(std::string path) {
        if (path.empty()) return;
        std::vector<std::string> one;
        one.push_back(std::move(path));
        Show(std::move(one), 0);
    }

    void ImagePreview::Show(std::vector<std::string> paths, int index) {
        if (paths.empty()) return;
        SetSources(std::move(paths), index);

        const bool wasOpen = open_;
        open_ = true;
        if (!wasOpen) {
            openAtMs_ = detail::NowMs();
            requestFocus();
        }
        syncOverlay();
        requestRepaint();
    }

    void ImagePreview::Close() {
        if (!open_) return;
        open_ = false;
        dragging_ = false;
        pressedPart_ = 0;
        hoverClose_ = hoverPrev_ = hoverNext_ = false;
        clearFocus();

        if (overlay_) overlay_->Hide();
        requestRepaint();
        if (onClosed_) onClosed_(*this);
    }

    void ImagePreview::SetSources(std::vector<std::string> paths, int index) {
        sources_ = std::move(paths);

        if (sources_.empty()) {
            index_ = -1;
            pendingIndex_ = 0;
        }
        else {
            index_ = (index < 0) ? 0
                   : (index >= static_cast<int>(sources_.size()) ? static_cast<int>(sources_.size()) - 1
                                                                 : index);
            pendingIndex_ = index_;
        }

        fitted_ = true;
        wantActual_ = false;
        wantZoom_ = 0.0f;
        scale_ = 0.0f;
        fitScaleCache_ = 0.0f;
        panX_ = panY_ = 0.0f;
        imgPxW_ = imgPxH_ = 0.0f;

        notifyIndexChanged(index_);
        requestRepaint();
    }

    void ImagePreview::AddSource(std::string path) {
        if (path.empty()) return;
        sources_.push_back(std::move(path));
        if (index_ < 0) {
            index_ = 0;
            fitted_ = true;
            wantActual_ = false;
            wantZoom_ = 0.0f;
            scale_ = 0.0f;
            panX_ = panY_ = 0.0f;
            imgPxW_ = imgPxH_ = 0.0f;
            notifyIndexChanged(index_);
        }
        requestRepaint();
    }

    void ImagePreview::Clear() {
        sources_.clear();
        index_ = -1;
        imgPxW_ = imgPxH_ = 0.0f;
        scale_ = 0.0f;
        panX_ = panY_ = 0.0f;
        notifyIndexChanged(-1);
        requestRepaint();
    }

    int ImagePreview::index() const {
        return index_;
    }

    void ImagePreview::SetIndex(int index) {
        if (sources_.empty()) return;
        const int n = static_cast<int>(sources_.size());
        const int i = (index < 0) ? 0 : (index >= n ? n - 1 : index);
        if (i == index_) return;

        index_ = i;
        fitted_ = true;
        wantActual_ = false;
        wantZoom_ = 0.0f;
        scale_ = 0.0f;
        fitScaleCache_ = 0.0f;
        panX_ = panY_ = 0.0f;
        imgPxW_ = imgPxH_ = 0.0f;

        notifyIndexChanged(index_);
        requestRepaint();
    }

    bool ImagePreview::Next() {
        if (sources_.empty()) return false;
        const int n = static_cast<int>(sources_.size());
        if (index_ + 1 < n) { SetIndex(index_ + 1); return true; }
        if (loop_ && n > 1) { SetIndex(0); return true; }
        return false;
    }

    bool ImagePreview::Previous() {
        if (sources_.empty()) return false;
        const int n = static_cast<int>(sources_.size());
        if (index_ > 0) { SetIndex(index_ - 1); return true; }
        if (loop_ && n > 1) { SetIndex(n - 1); return true; }
        return false;
    }

    const std::string& ImagePreview::source() const {
        static const std::string kEmpty;
        if (index_ < 0 || index_ >= static_cast<int>(sources_.size())) return kEmpty;
        return sources_[static_cast<std::size_t>(index_)];
    }

    void ImagePreview::SetLoop(bool on) {
        loop_ = on;
        requestRepaint();
    }

    float ImagePreview::fitScale() const {
        if (imgPxW_ <= 0.0f || imgPxH_ <= 0.0f) return 0.0f;
        Window* w = window();
        if (!w) return 0.0f;

        const float barTop = showTopBar_ ? topBarHeight_ : 0.0f;
        const float barBot = showBottomBar_ ? bottomBarHeight_ : 0.0f;
        const float vw = static_cast<float>(w->width()) - viewPadding_ * 2.0f;
        const float vh = static_cast<float>(w->height()) - barTop - barBot - viewPadding_ * 2.0f;
        if (vw <= 1.0f || vh <= 1.0f) return 0.0f;

        return std::min(vw / imgPxW_, vh / imgPxH_);
    }

    float ImagePreview::zoom() const {
        float f = (fitScaleCache_ > 0.0f) ? fitScaleCache_ : fitScale();
        if (f <= 0.0f) return 1.0f;
        const float s = (scale_ > 0.0f) ? scale_ : f;
        return s / f;
    }

    void ImagePreview::SetZoom(float zoomValue) {
        panX_ = panY_ = 0.0f;
        fitted_ = false;

        const float f = fitScale();
        if (f <= 0.0f) {
            wantZoom_ = (zoomValue > 0.0f) ? zoomValue : 1.0f;
            requestRepaint();
            return;
        }

        wantZoom_ = 0.0f;
        scale_ = Clamp(f * zoomValue, f * minZoom_, f * maxZoom_);
        clampView();
        requestRepaint();
    }

    void ImagePreview::zoomBy(float factor) {
        const float f = fitScale();
        if (f <= 0.0f) {
            const float base = (wantZoom_ > 0.0f) ? wantZoom_ : 1.0f;
            wantZoom_ = base * factor;
            fitted_ = false;
            requestRepaint();
            return;
        }

        const float s0 = (scale_ > 0.0f) ? scale_ : f;
        applyZoom(s0 * factor, -1.0f, -1.0f);
    }

    void ImagePreview::FitToWindow() {
        fitted_ = true;
        wantActual_ = false;
        wantZoom_ = 0.0f;
        scale_ = 0.0f;
        panX_ = panY_ = 0.0f;
        requestRepaint();
    }

    void ImagePreview::ActualSize() {
        fitted_ = false;
        panX_ = panY_ = 0.0f;

        const float f = fitScale();
        if (imgPxW_ <= 0.0f || f <= 0.0f) {
            wantActual_ = true;
            wantZoom_ = 0.0f;
            requestRepaint();
            return;
        }

        wantActual_ = false;
        wantZoom_ = 0.0f;
        scale_ = Clamp(1.0f, f * minZoom_, f * maxZoom_);
        clampView();
        requestRepaint();
    }

    void ImagePreview::ZoomIn() {
        zoomBy(zoomStep_);
    }

    void ImagePreview::ZoomOut() {
        zoomBy(1.0f / zoomStep_);
    }

    void ImagePreview::SetZoomStep(float step) {
        zoomStep_ = Clamp(step, 1.01f, 3.0f);
    }

    void ImagePreview::SetZoomRange(float minZoom, float maxZoom) {
        minZoom_ = (minZoom > 0.0f) ? minZoom : 0.1f;
        maxZoom_ = (maxZoom > minZoom_) ? maxZoom : minZoom_ * 2.0f;
    }

    void ImagePreview::CenterImage() {
        panX_ = panY_ = 0.0f;
        requestRepaint();
    }

    void ImagePreview::applyZoom(float newScale, float anchorX, float anchorY) {
        if (imgPxW_ <= 0.0f || imgPxH_ <= 0.0f) return;
        const float f = fitScale();
        if (f <= 0.0f) return;

        const float s0 = (scale_ > 0.0f) ? scale_ : f;
        if (s0 <= 0.0f) return;

        const Geom g = computeGeom();
        if (!g.valid) return;

        float ax = anchorX, ay = anchorY;
        if (ax < 0.0f || ay < 0.0f) {
            ax = g.vx + g.vw * 0.5f;
            ay = g.vy + g.vh * 0.5f;
        }

        const float u = (ax - g.ix) / s0;
        const float v = (ay - g.iy) / s0;

        const float s1 = Clamp(newScale, f * minZoom_, f * maxZoom_);
        if (s1 <= 0.0f) return;

        const float cx = ax + s1 * (imgPxW_ * 0.5f - u);
        const float cy = ay + s1 * (imgPxH_ * 0.5f - v);

        scale_ = s1;
        fitted_ = false;
        panX_ = cx - (g.vx + g.vw * 0.5f);
        panY_ = cy - (g.vy + g.vh * 0.5f);

        clampView();
        requestRepaint();
    }

    void ImagePreview::clampView() {
        const Geom g = computeGeom();
        if (!g.valid || g.iw <= 0.0f || g.ih <= 0.0f) return;

        const float ox = g.iw - g.vw;
        panX_ = (ox <= 0.0f) ? 0.0f : Clamp(panX_, -ox * 0.5f, ox * 0.5f);

        const float oy = g.ih - g.vh;
        panY_ = (oy <= 0.0f) ? 0.0f : Clamp(panY_, -oy * 0.5f, oy * 0.5f);
    }

    void ImagePreview::ensureImageSize(FrameCollector& fc) const {
        const std::string& p = source();
        if (p.empty()) {
            imgPxW_ = imgPxH_ = 0.0f;
            return;
        }
        float w = 0.0f, h = 0.0f;
        if (fc.ImageSize(p, w, h)) {
            imgPxW_ = w;
            imgPxH_ = h;
        }
        else {
            imgPxW_ = imgPxH_ = 0.0f;
        }
    }

    ImagePreview::Geom ImagePreview::computeGeom() const {
        Geom g;
        Window* w = window();
        if (!w) return g;

        g.winW = static_cast<float>(w->width());
        g.winH = static_cast<float>(w->height());
        if (g.winW <= 0.0f || g.winH <= 0.0f) return g;

        g.topH = showTopBar_ ? topBarHeight_ : 0.0f;
        g.botH = showBottomBar_ ? bottomBarHeight_ : 0.0f;
        g.vx = 0.0f;
        g.vy = g.topH;
        g.vw = g.winW;
        g.vh = std::max(1.0f, g.winH - g.topH - g.botH);

        if (showTopBar_ && showCloseButton_) {
            g.closeS = kCloseSize;
            g.closeX = g.winW - kCloseMargin - kCloseSize;
            g.closeY = (g.topH - kCloseSize) * 0.5f;
        }

        g.hasNav = showNavArrows_ && sources_.size() > 1;
        if (g.hasNav) {
            g.navS = kNavSize;
            g.navLx = kNavMargin + kNavSize * 0.5f;
            g.navRx = g.winW - kNavMargin - kNavSize * 0.5f;
            g.navY = g.vy + g.vh * 0.5f;
        }

        if (imgPxW_ > 0.0f && imgPxH_ > 0.0f) {
            const float f = fitScale();
            if (f > 0.0f) fitScaleCache_ = f;
            const float s = (fitted_ || scale_ <= 0.0f) ? (f > 0.0f ? f : 1.0f)
                                                        : scale_;
            const float dw = imgPxW_ * s;
            const float dh = imgPxH_ * s;
            const float cx = g.vx + g.vw * 0.5f + panX_;
            const float cy = g.vy + g.vh * 0.5f + panY_;
            g.ix = cx - dw * 0.5f;
            g.iy = cy - dh * 0.5f;
            g.iw = dw;
            g.ih = dh;
        }

        g.valid = true;
        return g;
    }

    bool ImagePreview::paintsOnTop() const {
        return open_ && !nativePopup();
    }

    bool ImagePreview::HitTest(float , float ) const {
        return open_;
    }

    bool ImagePreview::wantsAnimationFrame() const {
        if (!open_) return false;
        return (detail::NowMs() - openAtMs_) < static_cast<long long>(kFadeMs);
    }

    float ImagePreview::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return 0.0f;
    }

    float ImagePreview::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return 0.0f;
    }

    void ImagePreview::SetScrimColor(const Color4f& c) {
        scrim_ = c;
        requestRepaint();
    }

    void ImagePreview::SetShowTopBar(bool on) {
        showTopBar_ = on;
        requestRepaint();
    }

    void ImagePreview::SetShowBottomBar(bool on) {
        showBottomBar_ = on;
        requestRepaint();
    }

    void ImagePreview::SetShowCloseButton(bool on) {
        showCloseButton_ = on;
        requestRepaint();
    }

    void ImagePreview::SetShowNavArrows(bool on) {
        showNavArrows_ = on;
        requestRepaint();
    }

    void ImagePreview::SetTitle(std::string title) {
        title_ = std::move(title);
        requestRepaint();
    }

    void ImagePreview::SetDoubleClickTogglesFit(bool on) {
        doubleClickTogglesFit_ = on;
    }

    void ImagePreview::SetClickBackgroundCloses(bool on) {
        clickBgCloses_ = on;
    }

    void ImagePreview::SetOnClosed(SelfAction handler) {
        onClosed_ = std::move(handler);
    }

    void ImagePreview::SetOnIndexChanged(IndexAction handler) {
        onIndexChanged_ = std::move(handler);
    }

    void ImagePreview::notifyIndexChanged(int index) {
        if (onIndexChanged_) onIndexChanged_(*this, index);
    }

    Cursor ImagePreview::cursorAt(float x, float y) const {
        if (!open_) return Cursor::Arrow;

        const Geom g = computeGeom();
        if (!g.valid) return Cursor::Arrow;

        if (g.closeS > 0.0f &&
            drawutil::HitRect(x, y, g.closeX, g.closeY, g.closeS, g.closeS))
            return Cursor::Pointer;

        if (g.hasNav) {
            const float half = g.navS * 0.5f;
            if (drawutil::HitRect(x, y, g.navLx - half, g.navY - half, g.navS, g.navS) ||
                drawutil::HitRect(x, y, g.navRx - half, g.navY - half, g.navS, g.navS))
                return Cursor::Pointer;
        }

        return dragging_ ? Cursor::Grabbing : Cursor::Grab;
    }

    bool ImagePreview::OnMousePress(MouseEvent& e) {
        if (!open_) return false;

        const Geom g = computeGeom();
        if (!g.valid) return false;

        pressedPart_ = 0;
        if (e.button != MouseButton::Left) return true;

        const long long now = detail::NowMs();
        const bool isDouble = (now - lastClickMs_) <= static_cast<long long>(kDoubleClickMs) &&
            std::fabs(e.x - lastClickX_) < 4.0f && std::fabs(e.y - lastClickY_) < 4.0f;
        lastClickMs_ = now;
        lastClickX_ = e.x;
        lastClickY_ = e.y;

        if (g.closeS > 0.0f &&
            drawutil::HitRect(e.x, e.y, g.closeX, g.closeY, g.closeS, g.closeS)) {
            pressedPart_ = 1;
            return true;
        }

        if (g.hasNav) {
            const float half = g.navS * 0.5f;
            if (drawutil::HitRect(e.x, e.y, g.navLx - half, g.navY - half, g.navS, g.navS)) {
                pressedPart_ = 2;
                return true;
            }
            if (drawutil::HitRect(e.x, e.y, g.navRx - half, g.navY - half, g.navS, g.navS)) {
                pressedPart_ = 3;
                return true;
            }
        }

        if (isDouble && doubleClickTogglesFit_ && imgPxW_ > 0.0f) {
            if (fitted_) ActualSize();
            else         FitToWindow();
            return true;
        }

        dragging_ = true;
        dragLastX_ = e.x;
        dragLastY_ = e.y;
        return true;
    }

    bool ImagePreview::OnMouseRelease(MouseEvent& e) {
        if (!open_) return false;

        const int part = pressedPart_;
        pressedPart_ = 0;

        if (e.button != MouseButton::Left) return true;

        if (part == 1) {
            const Geom g = computeGeom();
            if (g.valid && g.closeS > 0.0f &&
                drawutil::HitRect(e.x, e.y, g.closeX, g.closeY, g.closeS, g.closeS))
                Close();
            return true;
        }
        if (part == 2) {
            const Geom g = computeGeom();
            if (g.valid) {
                const float half = g.navS * 0.5f;
                if (drawutil::HitRect(e.x, e.y, g.navLx - half, g.navY - half, g.navS, g.navS))
                    Previous();
            }
            return true;
        }
        if (part == 3) {
            const Geom g = computeGeom();
            if (g.valid) {
                const float half = g.navS * 0.5f;
                if (drawutil::HitRect(e.x, e.y, g.navRx - half, g.navY - half, g.navS, g.navS))
                    Next();
            }
            return true;
        }

        if (dragging_ && clickBgCloses_) {
            const Geom g = computeGeom();
            const bool onImage = g.valid && imgPxW_ > 0.0f &&
                drawutil::HitRect(e.x, e.y, g.ix, g.iy, g.iw, g.ih);
            const bool barelyMoved =
                std::fabs(e.x - dragLastX_) < 3.0f && std::fabs(e.y - dragLastY_) < 3.0f;
            if (!onImage && barelyMoved) {
                dragging_ = false;
                Close();
                return true;
            }
        }

        dragging_ = false;
        return true;
    }

    bool ImagePreview::OnMouseMove(MouseEvent& e) {
        if (!open_) return false;

        const Geom g = computeGeom();
        if (!g.valid) return true;

        const bool hc = g.closeS > 0.0f &&
            drawutil::HitRect(e.x, e.y, g.closeX, g.closeY, g.closeS, g.closeS);
        bool hp = false, hn = false;
        if (g.hasNav) {
            const float half = g.navS * 0.5f;
            hp = drawutil::HitRect(e.x, e.y, g.navLx - half, g.navY - half, g.navS, g.navS);
            hn = drawutil::HitRect(e.x, e.y, g.navRx - half, g.navY - half, g.navS, g.navS);
        }
        if (hc != hoverClose_ || hp != hoverPrev_ || hn != hoverNext_) {
            hoverClose_ = hc;
            hoverPrev_ = hp;
            hoverNext_ = hn;
            requestRepaint();
        }

        if (dragging_) {
            panX_ += e.x - dragLastX_;
            panY_ += e.y - dragLastY_;
            dragLastX_ = e.x;
            dragLastY_ = e.y;
            clampView();
            requestRepaint();
        }
        return true;
    }

    bool ImagePreview::OnMouseWheel(MouseEvent& e) {
        if (!open_) return false;

        const float f = fitScale();
        if (f <= 0.0f) return true;

        const float s0 = (scale_ > 0.0f) ? scale_ : f;
        const float steps = static_cast<float>(e.wheelDelta) / 120.0f;
        const float factor = std::pow(zoomStep_, steps);
        applyZoom(s0 * factor, e.x, e.y);
        return true;
    }

    void ImagePreview::OnMouseLeave() {
        if (hoverClose_ || hoverPrev_ || hoverNext_) {
            hoverClose_ = hoverPrev_ = hoverNext_ = false;
            requestRepaint();
        }
        BoxWidget::OnMouseLeave();
    }

    bool ImagePreview::OnKeyPress(KeyEvent& e) {
        if (!open_) return false;

        switch (e.key) {
        case Key::Escape:
            Close();
            return true;

        case Key::LeftArrow:
        case Key::PageUp:
            Previous();
            return true;

        case Key::RightArrow:
        case Key::PageDown:
            Next();
            return true;

        case Key::Equal:
        case Key::NumPadAdd:
            ZoomIn();
            return true;

        case Key::Minus:
        case Key::NumPadSubtract:
            ZoomOut();
            return true;

        case Key::Key0:
        case Key::Home:
            FitToWindow();
            return true;

        case Key::Key1:
        case Key::End:
            ActualSize();
            return true;

        default:
            break;
        }
        return false;
    }

    void ImagePreview::Paint(FrameCollector& fc) const {
        if (!open_) return;

        syncOverlay();
        if (nativePopup()) {
            overlay_->Repaint();
            Window* w = window();
            if (openAtMs_ > 0 && kFadeMs > 0.0f) {
                const float fade = Clamp(static_cast<float>(detail::NowMs() - openAtMs_) / kFadeMs, 0.0f, 1.0f);
                if (fade < 1.0f && w) w->RequestFrameWake(16);
            }
            return;
        }
        paintContent(fc);
    }

    void ImagePreview::paintContent(FrameCollector& fc) const {
        Window* win = window();
        if (!win) return;

        fc.ResetClip();

        ensureImageSize(fc);

        const float f = fitScale();
        fitScaleCache_ = f;
        if (f > 0.0f && (fitted_ || scale_ <= 0.0f)) scale_ = f;

        if (wantZoom_ > 0.0f && f > 0.0f) {
            scale_ = Clamp(f * wantZoom_, f * minZoom_, f * maxZoom_);
            wantZoom_ = 0.0f;
        }
        if (wantActual_ && f > 0.0f && imgPxW_ > 0.0f) {
            wantActual_ = false;
            scale_ = Clamp(1.0f, f * minZoom_, f * maxZoom_);
        }

        const Geom g = computeGeom();
        if (!g.valid) return;

        const long long now = detail::NowMs();
        float fade = 1.0f;
        if (openAtMs_ > 0 && kFadeMs > 0.0f) {
            fade = Clamp(static_cast<float>(now - openAtMs_) / kFadeMs, 0.0f, 1.0f);
            if (fade < 1.0f) win->RequestFrameWake(16);
        }
        if (fade <= 0.0f) return;

        const Color4f glyph = [&] {
            Color4f c = glyphColor_; c.a *= fade; return c;
        }();
        const Color4f hoverBg = [&] {
            Color4f c = glyphHoverBg_; c.a *= fade; return c;
        }();
        const Color4f titleColor = [&] {
            Color4f c = titleStyle_.colors.get(WidgetState::Normal).text; c.a *= fade; return c;
        }();
        const Color4f infoColor = [&] {
            Color4f c = infoStyle_.colors.get(WidgetState::Normal).text; c.a *= fade; return c;
        }();

        {
            Color4f sc = scrim_;
            sc.a *= fade;
            drawutil::PaintRoundedRect(fc, 0.0f, 0.0f, g.winW, g.winH, 0.0f, sc);
        }

        if (imgPxW_ > 0.0f && g.iw > 0.0f && g.ih > 0.0f) {
            fc.PushClip(g.vx, g.vy, g.vw, g.vh);
            fc.AddImage(source(), g.ix, g.iy, g.iw, g.ih,
                BgImageDrawMode::Stretch, BgImageHAlign::Center, BgImageVAlign::Middle,
                Color4f{ 1.0f, 1.0f, 1.0f, fade });
            fc.PopClip();
        }
        else if (!source().empty()) {
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Loading), infoStyle_.font,
                g.vx, g.vy + g.vh * 0.5f - 10.0f, g.vw, 20.0f, infoColor);
        }

        if (showTopBar_ && g.topH > 0.0f) {
            Color4f barBg = box_.colors.get(WidgetState::Normal).background;
            barBg.a *= fade;
            drawutil::PaintRoundedRect(fc, 0.0f, 0.0f, g.winW, g.topH, 0.0f, barBg);

            const std::string shown = title_.empty() ? fileNameOf(source()) : title_;
            if (!shown.empty()) {
                const float avail = g.winW - (showCloseButton_ ? (kCloseSize + kCloseMargin * 2.0f) : 40.0f)
                                  - 180.0f;
                const TextMetrics tm = fc.MeasureText(shown, titleStyle_.font);
                const std::string t = (tm.width <= avail || avail <= 0.0f)
                    ? shown
                    : drawutil::Ellipsize(fc, shown, titleStyle_.font, avail);
                drawutil::PaintTextAt(fc, t, titleStyle_.font, 16.0f,
                    (g.topH - titleStyle_.font.pixelSize * 1.4f) * 0.5f, titleColor);
            }

            if (sources_.size() > 1) {
                const std::string seq = std::to_string(index_ + 1) + " / " +
                                        std::to_string(sources_.size());
                const TextMetrics tm = fc.MeasureText(seq, infoStyle_.font);
                const float right = showCloseButton_ ? (g.closeX - 12.0f) : (g.winW - 16.0f);
                drawutil::PaintTextAt(fc, seq, infoStyle_.font, right - tm.width,
                    (g.topH - infoStyle_.font.pixelSize * 1.4f) * 0.5f, infoColor);
            }

            if (showCloseButton_ && g.closeS > 0.0f) {
                const float cx = g.closeX + g.closeS * 0.5f;
                const float cy = g.closeY + g.closeS * 0.5f;
                if (hoverClose_) {
                    PaintPanel(fc, g.closeX, g.closeY, g.closeS, g.closeS,
                        g.closeS * 0.5f, hoverBg);
                }
                const float arm = g.closeS * 0.26f;
                const float th = std::max(1.5f, g.closeS * 0.10f);
                drawutil::PaintThickSegment(fc, cx - arm, cy - arm, cx + arm, cy + arm, th, glyph);
                drawutil::PaintThickSegment(fc, cx + arm, cy - arm, cx - arm, cy + arm, th, glyph);
            }
        }

        if (showBottomBar_ && g.botH > 0.0f) {
            const float by = g.winH - g.botH;

            Color4f barBg = box_.colors.get(WidgetState::Normal).background;
            barBg.a *= fade;
            drawutil::PaintRoundedRect(fc, 0.0f, by, g.winW, g.botH, 0.0f, barBg);

            const float textY = by + (g.botH - infoStyle_.font.pixelSize * 1.4f) * 0.5f;

            {
                const float z = zoom();
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(z * 100.0f + 0.5f));
                drawutil::PaintTextAt(fc, buf, infoStyle_.font, 16.0f, textY, infoColor);
            }

            {
                const std::string hint = Kit::UiText(TextId::PreviewHint);
                const TextMetrics tm = fc.MeasureText(hint, infoStyle_.font);
                Color4f faded = infoColor;
                faded.a *= 0.75f;
                drawutil::PaintTextAt(fc, hint, infoStyle_.font,
                    (g.winW - tm.width) * 0.5f, textY, faded);
            }

            if (imgPxW_ > 0.0f && imgPxH_ > 0.0f) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d × %d",
                    static_cast<int>(imgPxW_), static_cast<int>(imgPxH_));
                const TextMetrics tm = fc.MeasureText(buf, infoStyle_.font);
                drawutil::PaintTextAt(fc, buf, infoStyle_.font,
                    g.winW - 16.0f - tm.width, textY, infoColor);
            }
        }

        if (g.hasNav) {
            const float half = g.navS * 0.5f;
            const bool canPrev = index_ > 0 || loop_;
            const bool canNext = index_ + 1 < static_cast<int>(sources_.size()) || loop_;

            const auto drawNav = [&](float cx, bool hovered, bool enabled, bool left) {
                if (!enabled) return;
                if (hovered)
                    drawutil::PaintCircle(fc, cx, g.navY, half, hoverBg);
                Color4f c = glyph;
                c.a *= (hovered ? 1.0f : 0.78f);
                drawutil::PaintChevron(fc, cx, g.navY, half * 0.34f,
                    std::max(2.0f, half * 0.11f),
                    left ? drawutil::ChevronDir::Left : drawutil::ChevronDir::Right, c);
            };
            drawNav(g.navLx, hoverPrev_, canPrev, true);
            drawNav(g.navRx, hoverNext_, canNext, false);
        }
    }

    std::string ImagePreview::fileNameOf(const std::string& path) const {
        if (path.empty()) return std::string();
        const std::size_t a = path.find_last_of("/\\");
        return (a == std::string::npos) ? path : path.substr(a + 1);
    }

    void ImagePreview::requestRepaint() {
        RequestWindowRedraw();
    }
}
