
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeBreadcrumb.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kSeparatorPad = 4.0f;
        constexpr float kSeparatorFallback = 8.0f;
        const char* const kGapLabel = "\xE2\x80\xA6";
    }

    const char* Breadcrumb::GapLabel() { return kGapLabel; }

    Breadcrumb::Breadcrumb() {
        SetAlign(AlignItems::Center);

        SetFocusable(true);

        SetDefaultSize(240.0f, 28.0f);
    }

    void Breadcrumb::SetCrumbs(const std::vector<std::string>& crumbs) {
        crumbs_ = crumbs;
        currentCrumb_ = crumbs_.empty() ? -1 : static_cast<int>(crumbs_.size()) - 1;
        rebuildItems();
        RequestWindowRedraw();
    }

    std::vector<std::string> Breadcrumb::crumbs() const {
        return crumbs_;
    }

    const std::string& Breadcrumb::crumb(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(crumbs_.size())) return kEmpty;
        return crumbs_[static_cast<std::size_t>(index)];
    }

    void Breadcrumb::SetCurrentCrumb(int index) {
        if (index < 0 || index >= static_cast<int>(crumbs_.size())) return;
        if (index == currentCrumb_) return;
        currentCrumb_ = index;
        syncCurrentItem();
        RequestWindowRedraw();
        RequestWindowRedraw();
    }

    int Breadcrumb::AddCrumb(std::string label) {
        crumbs_.push_back(std::move(label));
        currentCrumb_ = static_cast<int>(crumbs_.size()) - 1;
        rebuildItems();
        return static_cast<int>(crumbs_.size()) - 1;
    }

    void Breadcrumb::SetMaxCrumbs(int maxCount) {
        maxCrumbs_ = (maxCount > 0) ? maxCount : 0;
        rebuildItems();
        RequestWindowRedraw();
    }

    void Breadcrumb::SetSeparator(std::string sep) {
        separator_ = std::move(sep);
        Invalidate();
        RequestWindowRedraw();
    }

    void Breadcrumb::rebuildItems() {
        if (rebuilding_) return;
        rebuilding_ = true;

        std::vector<std::string> shown;

        const int n = static_cast<int>(crumbs_.size());
        if (maxCrumbs_ <= 0 || n <= maxCrumbs_ || maxCrumbs_ < 3) {
            shown = crumbs_;
        }
        else {
            const int tailCount = 2;
            shown.reserve(static_cast<std::size_t>(maxCrumbs_));
            shown.push_back(crumbs_[0]);
            shown.push_back(GapLabel());
            for (int i = n - tailCount; i < n; ++i)
                shown.push_back(crumbs_[static_cast<std::size_t>(i)]);
        }

        ItemStripBase::SetItems(shown);

        syncCurrentItem();

        rebuilding_ = false;
    }

    void Breadcrumb::syncCurrentItem() {
        int displayIdx = -1;
        if (currentCrumb_ >= 0 && currentCrumb_ < static_cast<int>(crumbs_.size())) {
            const std::string& want = crumbs_[static_cast<std::size_t>(currentCrumb_)];
            for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
                if (itemLabel(i) == want) { displayIdx = i; break; }
            }
        }
        if (displayIdx < 0 && itemCount() > 0)
            displayIdx = static_cast<int>(itemCount()) - 1;

        SetCurrentIndexInternal(displayIdx);

        for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
            if (itemLabel(i) == GapLabel()) SetItemEnabled(i, false);
        }
    }

    float Breadcrumb::separatorWidth() const {
        if (separator_.empty()) return 0.0f;

        const float tw = MeasureStripText(nullptr, separator_, styleTheme_.item.font);
        if (tw <= 0.0f) return kSeparatorFallback;
        return tw + kSeparatorPad * 2.0f;
    }

    void Breadcrumb::paintStripSeparator(FrameCollector& fc, const ItemRect& r) const {
        if (separator_.empty() || r.w <= 0.0f || r.h <= 0.0f) return;

        Color4f col = styleTheme_.separatorColor;
        if (col.a <= 0.0f) {
            col = styleTheme_.item.colors.get(WidgetState::Normal).text;
            col.a *= 0.45f;
        }

        const FontDesc& font = styleTheme_.item.font;
        const TextMetrics tm = fc.MeasureText(separator_, font);
        if (tm.width <= 0.0f) return;

        const float tx = r.x + (r.w - tm.width) * 0.5f;
        const float ty = r.y + (r.h - tm.height) * 0.5f;
        fc.AddText(separator_, font, tx, ty + tm.ascent, col);
    }

    void Breadcrumb::OnCurrentChangedByUser(int index) {
        if (index < 0 || index >= static_cast<int>(itemCount())) return;
        const std::string& label = itemLabel(index);
        if (label == GapLabel()) return;

        int pathIdx = -1;
        for (int i = 0; i < static_cast<int>(crumbs_.size()); ++i) {
            if (crumbs_[static_cast<std::size_t>(i)] == label) { pathIdx = i; break; }
        }
        if (pathIdx < 0) return;

        currentCrumb_ = pathIdx;
        syncCurrentItem();

        if (onNavigate_)     onNavigate_(*this, pathIdx);
        if (onNavigateSelf_) onNavigateSelf_(*this);
    }

}
