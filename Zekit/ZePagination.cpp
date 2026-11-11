
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "include/ZePagination.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        const char* const kPrevLabel = "\x01<";
        const char* const kNextLabel = "\x01>";
        const char* const kGapLabel = "\xE2\x80\xA6";

        constexpr int kMinButtons = 5;
        constexpr float kDefaultCell = 28.0f;
        constexpr float kArrowWidthFactor = 0.85f;
        constexpr float kChevronSize = 3.6f;
        constexpr float kChevronThickness = 1.5f;
    }

    Pagination::Pagination() {
        SetAlign(AlignItems::Center);
        SetFocusable(true);
        SetDefaultSize(280.0f, 32.0f);
    }

    void Pagination::SetPageCount(int totalPages) {
        totalPages_ = (totalPages > 0) ? totalPages : 0;
        if (totalPages_ > 0) page_ = std::clamp(page_, 1, totalPages_);
        else                 page_ = 1;
        rebuildItems();
        RequestWindowRedraw();
    }

    void Pagination::SetCurrentPage(int page) {
        if (totalPages_ <= 0) return;
        const int target = std::clamp(page, 1, totalPages_);

        if (target == page_) {
            syncCurrentItem();
            return;
        }
        page_ = target;
        rebuildItems();
        RequestWindowRedraw();
    }

    void Pagination::SetMaxButtons(int count) {
        maxButtons_ = (count > kMinButtons) ? count : kMinButtons;
        rebuildItems();
        RequestWindowRedraw();
    }

    void Pagination::SetShowEdges(bool on) {
        if (showEdges_ == on) return;
        showEdges_ = on;
        rebuildItems();
        RequestWindowRedraw();
    }

    void Pagination::SetShowArrows(bool on) {
        if (showArrows_ == on) return;
        showArrows_ = on;
        rebuildItems();
        RequestWindowRedraw();
    }

    bool Pagination::IsGap(const std::string& label) { return label == kGapLabel; }
    bool Pagination::IsPrev(const std::string& label) { return label == kPrevLabel; }
    bool Pagination::IsNext(const std::string& label) { return label == kNextLabel; }

    std::string Pagination::PageLabel(int page1Based) {
        return std::to_string(page1Based);
    }

    int Pagination::PageFromItem(const std::string& label, int curPage) {
        if (IsGap(label)) return 0;
        if (IsPrev(label)) return (curPage > 1) ? (curPage - 1) : 0;
        if (IsNext(label)) return curPage + 1;
        return std::atoi(label.c_str());
    }

    void Pagination::syncCurrentItem() {
        const std::string want = PageLabel(page_);
        int idx = -1;
        for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
            if (itemLabel(i) == want) { idx = i; break; }
        }
        SetCurrentIndexInternal(idx);

        if (idx >= 0) {
            for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
                const std::string& lb = itemLabel(i);
                if (IsPrev(lb))      SetItemEnabled(i, page_ > 1);
                else if (IsNext(lb)) SetItemEnabled(i, page_ < totalPages_);
                else if (IsGap(lb))  SetItemEnabled(i, false);
            }
        }
    }

    float Pagination::cellSize() const {
        return (styleTheme_.itemHeight > 0.0f) ? styleTheme_.itemHeight : kDefaultCell;
    }

    void Pagination::rebuildItems() {
        if (rebuilding_) return;
        rebuilding_ = true;

        const int wantPage = (totalPages_ > 0) ? std::clamp(page_, 1, totalPages_) : 1;

        std::vector<std::string> shown;

        if (totalPages_ <= 0) {
            ItemStripBase::SetItems(shown);
            SetCurrentIndexInternal(-1);
            rebuilding_ = false;
            return;
        }

        const bool arrows = showArrows_;
        if (arrows) {
            shown.push_back(kPrevLabel);
        }

        const int arrowSlots = arrows ? 2 : 0;
        const int pageSlots = std::max(1, maxButtons_ - arrowSlots);

        if (totalPages_ <= pageSlots) {
            for (int p = 1; p <= totalPages_; ++p) shown.push_back(PageLabel(p));
        }
        else {
            const bool edges = showEdges_;
            const int slotCount = pageSlots;
            const int capacity = (edges && slotCount >= 2) ? (slotCount - 2) : slotCount;

            int nearCap = capacity;
            if (edges) nearCap = std::max(1, nearCap - 2);
            if (nearCap > slotCount) nearCap = slotCount;

            std::vector<int> pages;
            const int center = std::clamp(wantPage, 1, totalPages_);

            pages.push_back(center);
            for (int step = 1; static_cast<int>(pages.size()) < nearCap; ++step) {
                bool added = false;
                const int right = center + step;
                const int left = center - step;

                if (right <= totalPages_ && static_cast<int>(pages.size()) < nearCap) {
                    pages.push_back(right);
                    added = true;
                }
                if (left >= 1 && static_cast<int>(pages.size()) < nearCap) {
                    pages.push_back(left);
                    added = true;
                }
                if (!added) break;
            }

            if (edges) {
                pages.push_back(1);
                pages.push_back(totalPages_);
            }

            std::sort(pages.begin(), pages.end());
            pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

            while (static_cast<int>(pages.size()) > slotCount) {
                int worst = -1;
                int worstDist = -1;
                for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
                    if (pages[i] == center) continue;
                    const int dist = std::abs(pages[i] - center);
                    if (dist > worstDist) { worstDist = dist; worst = i; }
                }
                if (worst < 0) break;
                pages.erase(pages.begin() + worst);
            }

            int prev = 0;
            for (int p : pages) {
                if (prev != 0 && p - prev > 1) shown.push_back(kGapLabel);
                shown.push_back(PageLabel(p));
                prev = p;
            }
        }

        if (arrows) {
            shown.push_back(kNextLabel);
        }

        ItemStripBase::SetItems(shown);

        syncCurrentItem();

        rebuilding_ = false;
    }

    float Pagination::measureStripItem(int index) const {
        const float cell = cellSize();
        if (index < 0 || index >= static_cast<int>(itemCount())) return cell;

        const std::string& lb = itemLabel(index);

        if (IsPrev(lb) || IsNext(lb)) return cell * kArrowWidthFactor;

        if (IsGap(lb)) return cell;

        const float padX = (styleTheme_.itemPaddingX > 0.0f) ? styleTheme_.itemPaddingX : 8.0f;
        const float textW = MeasureStripText(nullptr, lb, styleTheme_.item.font);
        if (textW <= 0.0f) return cell;
        return std::max(cell, textW + padX);
    }

    void Pagination::ApplyTheme(const Theme& t) {
        ItemStripBase::ApplyTheme(t);

        activeInk_ = t.onPrimary;
        if (activeInk_.a <= 0.0f)
            activeInk_ = styleTheme_.itemActive.colors.get(WidgetState::Normal).text;
    }

    void Pagination::paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const {
        if (index < 0 || index >= static_cast<int>(itemCount())) return;
        if (r.w <= 0.0f || r.h <= 0.0f) return;

        const std::string& lb = itemLabel(index);
        const bool enabled = isItemEnabled(index);
        const bool active = (index == currentIndex());

        if (IsGap(lb)) {
            Color4f col = styleTheme_.separatorColor;
            if (col.a <= 0.0f) {
                col = styleTheme_.item.colors.get(WidgetState::Normal).text;
                col.a *= 0.45f;
            }
            const float cx = r.x + r.w * 0.5f;
            const float cy = r.y + r.h * 0.5f;
            const float dot = 1.4f;
            drawutil::PaintCircle(fc, cx - dot * 3.0f, cy, dot, col);
            drawutil::PaintCircle(fc, cx, cy, dot, col);
            drawutil::PaintCircle(fc, cx + dot * 3.0f, cy, dot, col);
            return;
        }

        Color4f bg{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (active)                              bg = styleTheme_.itemActiveColor;
        else if (enabled && index == hoverIndex()) bg = styleTheme_.itemHoverColor;
        if (bg.a > 0.0f)
            drawutil::PaintRoundedRect(fc, r.x, r.y, r.w, r.h, styleTheme_.radius, bg);

        Color4f fg;
        if (!enabled) {
            fg = (styleTheme_.disabledColor.a > 0.0f)
                ? styleTheme_.disabledColor
                : styleTheme_.item.colors.get(WidgetState::Disabled).text;
        }
        else if (active) {
            fg = activeInk_;
        }
        else {
            fg = styleTheme_.item.colors.get(WidgetState::Normal).text;
        }

        if (IsPrev(lb) || IsNext(lb)) {
            const float cx = r.x + r.w * 0.5f;
            const float cy = r.y + r.h * 0.5f;
            const drawutil::ChevronDir dir =
                IsPrev(lb) ? drawutil::ChevronDir::Left : drawutil::ChevronDir::Right;
            drawutil::PaintChevron(fc, cx, cy, kChevronSize, kChevronThickness, dir, fg);
            return;
        }

        if (fg.a <= 0.0f) fg = styleTheme_.item.colors.get(WidgetState::Normal).text;

        const FontDesc& font = styleTheme_.item.font;
        const TextMetrics tm = fc.MeasureText(lb, font);
        if (tm.width <= 0.0f) return;

        const float tx = r.x + (r.w - tm.width) * 0.5f;
        const float ty = r.y + (r.h - tm.height) * 0.5f;
        fc.AddText(lb, font, tx, ty + tm.ascent, fg);
    }

    void Pagination::OnCurrentChangedByUser(int index) {
        if (totalPages_ <= 0) return;
        if (index < 0 || index >= static_cast<int>(itemCount())) return;

        const std::string& lb = itemLabel(index);
        if (IsGap(lb)) return;

        const int target = PageFromItem(lb, page_);
        if (target <= 0) return;
        const int clamped = std::clamp(target, 1, totalPages_);
        if (clamped == page_) return;

        page_ = clamped;
        rebuildItems();

        if (onPageChanged_)     onPageChanged_(*this, page_);
        if (onPageChangedSelf_) onPageChangedSelf_(*this);
    }

}
