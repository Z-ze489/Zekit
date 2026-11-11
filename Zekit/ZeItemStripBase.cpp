
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ZeItemStripBase.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kDefaultItemHeight = 28.0f;
        constexpr float kDefaultPaddingX = 10.0f;
        constexpr float kDefaultGap = 6.0f;
        constexpr float kMinSeparator = 4.0f;
        constexpr float kFallbackW = 240.0f;
        constexpr float kFallbackH = 32.0f;
        constexpr float kActiveTintAlpha = 0.14f;
    }

    ItemStripBase::ItemStripBase() {
        SetDirection(FlexDirection::Row);
        SetAlign(AlignItems::Center);

        ApplyTheme(Themes::Light());

        SetDefaultSize(kFallbackW, kFallbackH);
    }

    void ItemStripBase::SetOrientation(StripOrientation o) {
        if (orientation_ == o) return;
        orientation_ = o;

        SetDirection(isVertical() ? FlexDirection::Column : FlexDirection::Row);
        SetAlign(isVertical() ? AlignItems::Stretch : AlignItems::Center);

        invalidateGeometry();
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    int ItemStripBase::AddItem(std::string label) {
        return AddItem(std::move(label), true);
    }

    int ItemStripBase::AddItem(std::string label, bool enabled) {
        Item it;
        it.label = std::move(label);
        it.enabled = enabled;
        items_.push_back(std::move(it));

        if (current_ < 0) current_ = 0;

        invalidateGeometry();
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
        return static_cast<int>(items_.size()) - 1;
    }

    void ItemStripBase::SetItems(const std::vector<std::string>& labels) {
        items_.clear();
        items_.reserve(labels.size());
        for (const std::string& s : labels) {
            Item it;
            it.label = s;
            it.enabled = true;
            items_.push_back(std::move(it));
        }

        if (items_.empty())        current_ = -1;
        else if (current_ < 0)     current_ = 0;
        else if (current_ >= static_cast<int>(items_.size()))
            current_ = static_cast<int>(items_.size()) - 1;

        invalidateGeometry();
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void ItemStripBase::ClearItems() {
        items_.clear();
        current_ = -1;
        hover_ = -1;
        pressed_ = -1;
        invalidateGeometry();
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void ItemStripBase::SetItemLabel(int index, std::string label) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        items_[static_cast<std::size_t>(index)].label = std::move(label);
        invalidateGeometry();
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    const std::string& ItemStripBase::itemLabel(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(items_.size())) return kEmpty;
        return items_[static_cast<std::size_t>(index)].label;
    }

    bool ItemStripBase::isItemEnabled(int index) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return false;
        return items_[static_cast<std::size_t>(index)].enabled;
    }

    void ItemStripBase::SetItemEnabled(int index, bool enabled) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        items_[static_cast<std::size_t>(index)].enabled = enabled;
        requestRepaint();
        RequestWindowRedraw();
    }

    void ItemStripBase::SetCurrentIndex(int index) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        if (index == current_) return;

        current_ = index;
        invalidateGeometry();
        requestRepaint();
        RequestWindowRedraw();
    }

    void ItemStripBase::selectByUser(int index) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        if (index == current_) return;
        if (!isItemEnabled(index)) return;

        current_ = index;
        invalidateGeometry();
        OnCurrentChangedByUser(index);
        notifyCurrentChanged(index);
        requestRepaint();
    }

    void ItemStripBase::notifyCurrentChanged(int index) {
        if (onChanged_)     onChanged_(*this, index);
        if (onChangedSelf_) onChangedSelf_(*this);
    }

    void ItemStripBase::requestRepaint() {
        RequestWindowRedraw();
    }

    void ItemStripBase::ApplyTheme(const Theme& t) {
        styleTheme_ = t.itemStrip;
        invalidateGeometry();
        Invalidate();
    }

    const TextStyle& ItemStripBase::itemTextStyle(int index) const {
        if (index == current_) return styleTheme_.itemActive;
        return styleTheme_.item;
    }

    float ItemStripBase::MeasureStripText(FrameCollector* fc, std::string_view text,
        const FontDesc& font) const {
        if (text.empty()) return 0.0f;

        if (fc) return fc->MeasureText(text, font).width;

        if (Window* w = window()) return w->MeasureText(text, font).width;

        return 0.0f;
    }

    void ItemStripBase::syncStripArea() const {
        const float av = contentAvailableW();
        const float ah = contentAvailableH();
        const float ax = contentLeft();
        const float ay = contentTop();
        if (av != cachedWidth_ || ah != cachedHeight_ || ax != cachedLeft_ || ay != cachedTop_) {
            cachedWidth_ = av;
            cachedHeight_ = ah;
            cachedLeft_ = ax;
            cachedTop_ = ay;
            geomDirty_ = true;
        }
    }

    void ItemStripBase::ensureGeometry() const {
        syncStripArea();
        if (!geomDirty_) return;

        geom_.clear();
        geomUsed_ = 0.0f;
        geomDirty_ = false;

        if (items_.empty()) return;

        float ax = 0.0f, ay = 0.0f, aw = 0.0f, ah = 0.0f;
        stripArea(ax, ay, aw, ah);
        if (aw <= 0.0f || ah <= 0.0f) return;

        const float sepW = std::max(0.0f, separatorWidth());
        const float itemH = (styleTheme_.itemHeight > 0.0f) ? styleTheme_.itemHeight : ah;

        geom_.reserve(items_.size());

        if (!isVertical()) {
            const float h = std::min(itemH, ah);
            const float ty = ay + (ah - h) * 0.5f;
            float cx = ax;
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i > 0 && sepW > 0.0f) cx += sepW;

                const float iw = std::max(0.0f, measureStripItem(static_cast<int>(i)));

                ItemRect r;
                r.x = cx; r.y = ty; r.w = iw; r.h = h;
                geom_.push_back(r);

                cx += iw;
                geomUsed_ = cx - ax;
            }
        }
        else {
            const float w = aw;
            const float rowH = std::min(itemH, ah);
            float cy = ay;
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i > 0 && sepW > 0.0f) cy += sepW;

                const float ih = std::max(rowH, measureStripItem(static_cast<int>(i)));

                ItemRect r;
                r.x = ax; r.y = cy; r.w = w; r.h = ih;
                geom_.push_back(r);

                cy += ih;
                geomUsed_ = cy - ay;
            }
        }
    }

    bool ItemStripBase::itemRect(int index, ItemRect& out) const {
        ensureGeometry();
        if (index < 0 || index >= static_cast<int>(geom_.size())) return false;
        out = geom_[static_cast<std::size_t>(index)];
        return true;
    }

    int ItemStripBase::itemAtPoint(float px, float py) const {
        ensureGeometry();
        for (std::size_t i = 0; i < geom_.size(); ++i) {
            const ItemRect& r = geom_[i];
            if (drawutil::HitRect(px, py, r.x, r.y, r.w, r.h)) return static_cast<int>(i);
        }
        return -1;
    }

    void ItemStripBase::stripArea(float& x, float& y, float& w, float& h) const {
        x = contentLeft();
        y = contentTop();
        w = contentAvailableW();
        h = contentAvailableH();
    }

    float ItemStripBase::measuredStripWidth() const {
        ensureGeometry();
        return geomUsed_;
    }

    float ItemStripBase::measureStripItem(int index) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return 0.0f;

        const float padX = (styleTheme_.itemPaddingX > 0.0f) ? styleTheme_.itemPaddingX : kDefaultPaddingX;
        const float minW = (styleTheme_.itemHeight > 0.0f) ? styleTheme_.itemHeight : kDefaultItemHeight;

        const Item& it = items_[static_cast<std::size_t>(index)];
        const float textW = MeasureStripText(nullptr, it.label, itemTextStyle(index).font);

        if (textW <= 0.0f) return minW;

        return std::max(minW, textW + padX * 2.0f);
    }

    void ItemStripBase::paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        if (r.w <= 0.0f || r.h <= 0.0f) return;

        const Item& it = items_[static_cast<std::size_t>(index)];
        const bool active = (index == current_);
        const bool enabled = it.enabled;

        Color4f bg{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (active)                                  bg = styleTheme_.itemActiveColor;
        else if (enabled && index == hover_)         bg = styleTheme_.itemHoverColor;

        if (active && bg.a > 0.0f)
            bg.a = std::min(bg.a, kActiveTintAlpha);

        if (bg.a > 0.0f)
            drawutil::PaintRoundedRect(fc, r.x, r.y, r.w, r.h, styleTheme_.radius, bg);

        const TextStyle& ts = itemTextStyle(index);
        Color4f fg = ts.colors.get(WidgetState::Normal).text;

        if (!enabled && styleTheme_.disabledColor.a > 0.0f)
            fg = styleTheme_.disabledColor;

        const FontDesc& font = ts.font;
        const float textAvail = std::max(0.0f, r.w - 6.0f);

        std::string shown = it.label;
        TextMetrics tm = fc.MeasureText(shown, font);
        if (tm.width > textAvail && textAvail > 0.0f) {
            shown = drawutil::Ellipsize(fc, it.label, font, textAvail);
            tm = fc.MeasureText(shown, font);
        }
        if (tm.width <= 0.0f) return;

        const float tx = r.x + (r.w - tm.width) * 0.5f;
        const float ty = r.y + (r.h - tm.height) * 0.5f;
        fc.AddText(shown, font, tx, ty + tm.ascent, fg);
    }

    float ItemStripBase::separatorWidth() const {
        return 0.0f;
    }

    void ItemStripBase::paintStripSeparator(FrameCollector& fc, const ItemRect& r) const {
        (void)fc;
        (void)r;
    }

    void ItemStripBase::paintStripBackground(FrameCollector& fc) const {
        (void)fc;
    }

    float ItemStripBase::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        const float padX = (styleTheme_.itemPaddingX > 0.0f) ? styleTheme_.itemPaddingX : kDefaultPaddingX;
        const float minW = (styleTheme_.itemHeight > 0.0f) ? styleTheme_.itemHeight : kDefaultItemHeight;
        const float sepW = std::max(0.0f, separatorWidth());
        const LayoutPadding& p = padding();

        if (isVertical()) {
            float widest = 0.0f;
            for (std::size_t i = 0; i < items_.size(); ++i) {
                const float textW = MeasureStripText(nullptr, items_[i].label,
                    itemTextStyle(static_cast<int>(i)).font);
                widest = std::max(widest, (textW <= 0.0f) ? minW : textW + padX * 2.0f);
            }
            return widest + p.left + p.right;
        }

        float total = 0.0f;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (i > 0) total += sepW;
            const float textW = MeasureStripText(nullptr, items_[i].label,
                itemTextStyle(static_cast<int>(i)).font);
            total += (textW <= 0.0f) ? minW : std::max(minW, textW + padX * 2.0f);
        }
        return total + p.left + p.right;
    }

    float ItemStripBase::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        const float itemH = (styleTheme_.itemHeight > 0.0f) ? styleTheme_.itemHeight : kDefaultItemHeight;
        const LayoutPadding& p = padding();

        if (isVertical()) {
            const float sepW = std::max(0.0f, separatorWidth());
            float total = 0.0f;
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (i > 0) total += sepW;
                total += std::max(itemH, measureStripItem(static_cast<int>(i)));
            }
            return total + p.top + p.bottom;
        }

        return itemH + p.top + p.bottom;
    }

    void ItemStripBase::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        Panel::Paint(fc);

        ensureGeometry();

        paintStripBackground(fc);

        const float sepW = std::max(0.0f, separatorWidth());
        if (sepW > 0.0f) {
            float ax = 0.0f, ay = 0.0f, aw = 0.0f, ah = 0.0f;
            stripArea(ax, ay, aw, ah);
            for (std::size_t i = 1; i < geom_.size(); ++i) {
                ItemRect sr;
                if (!isVertical()) {
                    sr.x = geom_[i - 1].x + geom_[i - 1].w;
                    sr.y = ay;
                    sr.w = sepW;
                    sr.h = ah;
                }
                else {
                    sr.x = ax;
                    sr.y = geom_[i - 1].y + geom_[i - 1].h;
                    sr.w = aw;
                    sr.h = sepW;
                }
                paintStripSeparator(fc, sr);
            }
        }

        for (std::size_t i = 0; i < geom_.size(); ++i)
            paintStripItem(fc, static_cast<int>(i), geom_[i]);
    }

    bool ItemStripBase::HitTest(float px, float py) const {
        return itemAtPoint(px, py) >= 0;
    }

    bool ItemStripBase::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (!isEnabled()) return false;

        const int i = itemAtPoint(e.x, e.y);
        if (i < 0 || !isItemEnabled(i)) return false;

        pressed_ = i;
        e.accept();
        return true;
    }

    bool ItemStripBase::OnMouseRelease(MouseEvent& e) {
        if (pressed_ < 0) return false;

        const int was = pressed_;
        pressed_ = -1;

        if (itemAtPoint(e.x, e.y) == was) selectByUser(was);

        e.accept();
        return true;
    }

    bool ItemStripBase::OnMouseMove(MouseEvent& e) {
        if (!isEnabled()) {
            if (hover_ == -1) return false;
            hover_ = -1;
            requestRepaint();
            return true;
        }
        const int i = itemAtPoint(e.x, e.y);
        if (i == hover_) return false;

        hover_ = i;
        requestRepaint();
        return true;
    }

    void ItemStripBase::OnMouseLeave() {
        if (hover_ == -1) return;
        hover_ = -1;
        requestRepaint();
    }

    bool ItemStripBase::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        const int n = static_cast<int>(items_.size());
        if (n <= 0) return false;

        const Key prevKey = isVertical() ? Key::UpArrow : Key::LeftArrow;
        const Key nextKey = isVertical() ? Key::DownArrow : Key::RightArrow;

        int target = current_;
        switch (e.key) {
        case Key::LeftArrow:
        case Key::RightArrow:
        case Key::UpArrow:
        case Key::DownArrow:
            if (e.key == prevKey)      target = (current_ < 0) ? 0 : current_ - 1;
            else if (e.key == nextKey) target = (current_ < 0) ? 0 : current_ + 1;
            else return false;
            break;
        case Key::Home:       target = 0; break;
        case Key::End:        target = n - 1; break;
        default:
            return false;
        }

        const int dir = (target >= current_) ? 1 : -1;
        int found = -1;
        for (int i = std::clamp(target, 0, n - 1); i >= 0 && i < n; i += dir) {
            if (isItemEnabled(i)) { found = i; break; }
        }
        if (found < 0) { e.accept(); return true; }

        selectByUser(found);
        e.accept();
        return true;
    }

}
