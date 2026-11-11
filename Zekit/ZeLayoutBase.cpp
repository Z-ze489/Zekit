#include "pch.h"
#include "include/ZeLayoutBase.h"
#include "include/Zekit.h"

#include <algorithm>

namespace Kit {

    void LayoutBase::SetDirection(FlexDirection d) {
        if (style_.direction == d) return;
        style_.direction = d;
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetJustify(JustifyContent j) {
        if (style_.justifyContent == j) return;
        style_.justifyContent = j;
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetAlign(AlignItems a) {
        if (style_.alignItems == a) return;
        style_.alignItems = a;
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetSpacing(float spacing) {
        const float v = (spacing > 0.0f) ? spacing : 0.0f;
        if (style_.columnGap == v && style_.rowGap == v) return;
        style_.rowGap = v;
        style_.columnGap = v;
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetGap(float rowGap, float columnGap) {
        const float r = (rowGap > 0.0f) ? rowGap : 0.0f;
        const float c = (columnGap > 0.0f) ? columnGap : 0.0f;
        if (style_.rowGap == r && style_.columnGap == c) return;
        style_.rowGap = r;
        style_.columnGap = c;
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetPadding(float left, float top, float right, float bottom) {
        const float l = (left > 0.0f) ? left : 0.0f;
        const float t = (top > 0.0f) ? top : 0.0f;
        const float r = (right > 0.0f) ? right : 0.0f;
        const float b = (bottom > 0.0f) ? bottom : 0.0f;
        if (padding_.left == l && padding_.top == t &&
            padding_.right == r && padding_.bottom == b) return;
        padding_ = { l, t, r, b };
        Invalidate();
        RequestWindowRedraw();
    }

    void LayoutBase::SetPadding(float horizontal, float vertical) {
        SetPadding(horizontal, vertical, horizontal, vertical);
        RequestWindowRedraw();
    }

    void LayoutBase::Invalidate() {
        dirty_ = true;

        if (!laying_) {
            noteHoverResync();
        }

        if (!laying_) {
            RequestWindowRedraw();
        }
    }

    void LayoutBase::OnLayoutInputChanged() {
        if (laying_) return;
        if (propagating_) return;

        Invalidate();

        {
            const PropagatingGuard guard(propagating_);
            notifyLayoutInputChanged();
        }

        if (onLayoutChanged_)     onLayoutChanged_();
        if (selfOnLayoutChanged_) selfOnLayoutChanged_(*this);
    }

    void LayoutBase::EnsureLayout() {
        const bool geoChanged = (cacheX_ != x() || cacheY_ != y() ||
                                 cacheW_ != width() || cacheH_ != height());
        const bool childChanged = (lastChildCount_ != children().size());
        if (!dirty_ && !geoChanged && !childChanged) return;

        cacheX_ = x(); cacheY_ = y(); cacheW_ = width(); cacheH_ = height();
        lastChildCount_ = children().size();
        dirty_ = false;

        noteHoverResync();

        for (Widget* c : children()) LayoutAccess::RestorePref(*c);

        LayoutScope scope(laying_);
        DoLayout();
    }

    float LayoutBase::contentAvailableW() const {
        return std::max(0.0f, width() - padding_.left - padding_.right - reservedRight());
    }

    float LayoutBase::contentAvailableH() const {
        return std::max(0.0f, height() - padding_.top - padding_.bottom - reservedBottom());
    }

    std::vector<Widget*> LayoutBase::visibleChildren() const {
        std::vector<Widget*> out;
        out.reserve(children().size());
        for (Widget* c : children()) {
            if (c && c->isVisible()) out.push_back(c);
        }
        return out;
    }

    float LayoutBase::stripHintWidth(float gap) const {
        const std::vector<Widget*> kids = visibleChildren();
        float total = 0.0f;
        for (std::size_t i = 0; i < kids.size(); ++i) {
            if (i > 0) total += gap;
            const Margin& m = kids[i]->margin();
            total += effectiveWidth(kids[i]) + m.left + m.right;
        }
        const LayoutPadding& pad = padding();
        return total + pad.left + pad.right;
    }

    float LayoutBase::stripHintHeight() const {
        const std::vector<Widget*> kids = visibleChildren();
        float tallest = 0.0f;
        for (Widget* c : kids) {
            const Margin& m = c->margin();
            tallest = std::max(tallest, effectiveHeight(c) + m.top + m.bottom);
        }
        const LayoutPadding& pad = padding();
        return tallest + pad.top + pad.bottom;
    }

    float LayoutBase::hintWidth() const {
        const std::vector<Widget*> items = visibleChildren();
        const float inner = padding_.left + padding_.right;

        if (items.empty()) return inner;

        if (style_.direction == FlexDirection::Row) {
            float sum = 0.0f;
            for (Widget* c : items) sum += effectiveWidth(c) + c->margin().left + c->margin().right;
            sum += style_.columnGap * static_cast<float>(items.size() - 1);
            return sum + inner;
        }

        float widest = 0.0f;
        for (Widget* c : items) {
            const Margin& m = c->margin();
            widest = std::max(widest, effectiveWidth(c) + m.left + m.right);
        }
        return widest + inner;
    }

    float LayoutBase::hintHeight() const {
        const std::vector<Widget*> items = visibleChildren();
        const float inner = padding_.top + padding_.bottom;

        if (items.empty()) return inner;

        if (style_.direction == FlexDirection::Column) {
            float sum = 0.0f;
            for (Widget* c : items) sum += effectiveHeight(c) + c->margin().top + c->margin().bottom;
            sum += style_.rowGap * static_cast<float>(items.size() - 1);
            return sum + inner;
        }

        float tallest = 0.0f;
        for (Widget* c : items) {
            const Margin& m = c->margin();
            tallest = std::max(tallest, effectiveHeight(c) + m.top + m.bottom);
        }
        return tallest + inner;
    }

    bool LayoutBase::heightDependsOnWidth() const {
        for (const Widget* c : children()) {
            if (!c || !c->isVisible()) continue;
            if (c->heightDependsOnWidth()) return true;
        }
        return false;
    }

    float LayoutBase::hintHeightFor(float availWidth) const {
        if (style_.direction != FlexDirection::Column) {
            const std::vector<Widget*> items = visibleChildren();
            if (items.empty() || availWidth <= 0.0f) return hintHeight();

            const float inner = std::max(
                availWidth - padding_.left - padding_.right - reservedRight(), 0.0f);

            float fixed = (items.size() > 1)
                ? style_.columnGap * static_cast<float>(items.size() - 1) : 0.0f;
            int nWrap = 0;
            for (Widget* c : items) {
                const Margin& m = c->margin();
                if (c->heightDependsOnWidth() && c->prefWidth() <= 0.0f) {
                    ++nWrap;
                } else {
                    fixed += effectiveWidth(c) + m.left + m.right;
                }
            }
            if (nWrap == 0) return hintHeight();

            measuredH_.clear();
            const float each = std::max(inner - fixed, 0.0f) / static_cast<float>(nWrap);
            for (Widget* c : items) {
                if (!c->heightDependsOnWidth() || c->prefWidth() > 0.0f) continue;
                const Margin& m = c->margin();
                const float w = std::max(each - m.left - m.right, 0.0f);
                measuredH_[c] = c->hintHeightFor(w);
            }
            return hintHeight();
        }

        const float innerW = std::max(
            availWidth - padding_.left - padding_.right - reservedRight(), 0.0f);

        for (Widget* c : children()) {
            if (!c || !c->isVisible()) continue;
            if (c->prefHeight() > 0.0f) continue;
            if (!c->heightDependsOnWidth()) continue;
            const Margin& m = c->margin();
            measuredH_[c] = c->hintHeightFor(std::max(innerW - m.left - m.right, 0.0f));
        }
        return hintHeight();
    }

    void LayoutBase::measureWidthDependentHeights() {
        measuredH_.clear();

        const float availW = contentAvailableW();

        for (Widget* c : visibleChildren()) {
            if (!c) continue;

            if (c->prefHeight() > 0.0f) continue;

            if (!c->heightDependsOnWidth()) continue;

            const float marginX = c->margin().left + c->margin().right;
            measuredH_[c] = c->hintHeightFor(availW - marginX);
        }
    }

    void LayoutBase::mainAxisDistribution(float freeLen, size_t n, float& lead, float& gap) const {
        lead = 0.0f;
        gap = 0.0f;
        if (freeLen < 0.0f) freeLen = 0.0f;

        switch (style_.justifyContent) {
        case JustifyContent::Start:
            lead = 0.0f;
            break;
        case JustifyContent::End:
            lead = freeLen;
            break;
        case JustifyContent::Center:
            lead = freeLen * 0.5f;
            break;
        case JustifyContent::SpaceBetween:
            if (n > 1) gap = freeLen / static_cast<float>(n - 1);
            else       lead = freeLen * 0.5f;
            break;
        case JustifyContent::SpaceAround:
            if (n > 0) {
                gap = freeLen / static_cast<float>(n);
                lead = gap * 0.5f;
            }
            break;
        case JustifyContent::SpaceEvenly:
            if (n > 0) {
                const float unit = freeLen / static_cast<float>(n + 1);
                lead = unit;
                gap = unit;
            }
            break;
        }
    }

    void LayoutBase::alignCrossAxis(AlignItems align, float crossLen, float childCrossSize,
                                    float& pos, float& size) const {
        if (crossLen < 0.0f) crossLen = 0.0f;
        switch (align) {
        case AlignItems::Start:
        case AlignItems::Baseline:
            pos = 0.0f;
            size = childCrossSize;
            break;
        case AlignItems::End:
            pos = crossLen - childCrossSize;
            size = childCrossSize;
            break;
        case AlignItems::Center:
            pos = (crossLen - childCrossSize) * 0.5f;
            size = childCrossSize;
            break;
        case AlignItems::Stretch:
            pos = 0.0f;
            size = crossLen;
            break;
        }
    }

    bool LayoutBase::clipChildrenRect(float& x, float& y, float& w, float& h) const {
        x = contentLeft();
        y = contentTop();
        w = contentAvailableW();
        h = contentAvailableH();
        return true;
    }

    void LayoutBase::setScrollOffset(float sx, float sy) {
        scrollOffsetX_ = sx;
        scrollOffsetY_ = sy;
    }

    void LayoutBase::layoutLinear(bool vertical) {
        const std::vector<Widget*> items = visibleChildren();

        for (Widget* c : items) LayoutAccess::RestorePref(*c);

        if (vertical) measureWidthDependentHeights();

        const float availW = contentAvailableW();
        const float availH = contentAvailableH();
        const float cx = contentLeft();
        const float cy = contentTop();

        const float crossLen = vertical ? availW : availH;
        const float mainGap  = vertical ? style_.rowGap : style_.columnGap;

        auto mainSize  = [&](Widget* c) -> float {
            const Margin& m = c->margin();
            return vertical ? (effectiveHeight(c) + m.top + m.bottom)
                            : (effectiveWidth(c)  + m.left + m.right);
        };
        auto crossSize = [&](Widget* c) -> float {
            return vertical ? effectiveWidth(c) : effectiveHeight(c);
        };
        auto actualCrossSize = [&](Widget* c) -> float {
            return vertical ? c->width() : c->height();
        };
        auto setCrossSize = [&](Widget* c, float v) {
            if (vertical) LayoutAccess::SetActual(*c, v, c->height());
            else          LayoutAccess::SetActual(*c, c->width(), v);
        };
        auto crossStartMargin = [&](Widget* c) -> float {
            return vertical ? c->margin().left : c->margin().top;
        };
        auto crossEndMargin = [&](Widget* c) -> float {
            return vertical ? c->margin().right : c->margin().bottom;
        };

        auto mainPolicy = [&](const Widget* c) -> SizePolicy {
            return vertical ? c->verticalPolicy() : c->horizontalPolicy();
        };
        auto isViewport = [&](const Widget* c) -> bool {
            if (c->flexGrow() > 0.0f) return false;
            if (mainPolicy(c) != SizePolicy::Expanding) return false;
            return (vertical ? c->prefHeight() : c->prefWidth()) <= 0.0f;
        };
        std::vector<size_t> expanders;
        for (size_t i = 0; i < items.size(); ++i)
            if (isViewport(items[i])) expanders.push_back(i);

        float totalMain = 0.0f;
        float growTotal = 0.0f;
        for (size_t i = 0; i < items.size(); ++i) {
            growTotal += items[i]->flexGrow();
            if (!isViewport(items[i])) totalMain += mainSize(items[i]);
        }
        if (!items.empty()) totalMain += mainGap * static_cast<float>(items.size() - 1);

        float freeLen = (vertical ? availH : availW) - totalMain;

        std::vector<float> growExtra(items.size(), 0.0f);
        float lead = 0.0f, extraGap = 0.0f;
        if (growTotal > 0.0f && freeLen > 0.0f) {
            std::vector<bool>  active(items.size(), false);
            std::vector<float> cap(items.size(), 0.0f);
            float activeGrow = 0.0f;
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i]->flexGrow() <= 0.0f) continue;
                const float cur = vertical ? items[i]->height() : items[i]->width();
                const float mx  = vertical ? items[i]->maxHeight() : items[i]->maxWidth();
                cap[i] = std::max(mx - cur, 0.0f);
                if (cap[i] > 0.0f) { active[i] = true; activeGrow += items[i]->flexGrow(); }
            }

            float remaining = freeLen;
            for (size_t round = 0; round <= items.size(); ++round) {
                if (remaining <= 0.0f || activeGrow <= 0.0f) break;

                const float denom = activeGrow;

                float requested = 0.0f;
                for (size_t i = 0; i < items.size(); ++i) {
                    if (!active[i]) continue;
                    const float share = remaining * (items[i]->flexGrow() / denom);
                    const float give  = std::min(share, cap[i]);
                    growExtra[i] += give;
                    cap[i]       -= give;
                    requested    += give;
                }

                for (size_t i = 0; i < items.size(); ++i) {
                    if (active[i] && cap[i] <= 0.0f) {
                        active[i] = false;
                        activeGrow -= items[i]->flexGrow();
                    }
                }

                if (requested <= 0.0f) break;
                remaining -= requested;
            }

            freeLen = remaining;
        }
        else if (freeLen < 0.0f) {
            std::vector<size_t> victims;
            for (size_t i = 0; i < items.size(); ++i) {
                const bool elastic = items[i]->flexGrow() > 0.0f;
                const bool wraps = !vertical && items[i]->heightDependsOnWidth();

                if (!elastic && !wraps) continue;
                if ((vertical ? items[i]->prefHeight() : items[i]->prefWidth()) > 0.0f)
                    continue;
                victims.push_back(i);
            }
            if (!victims.empty()) {
                const size_t n = victims.size();
                std::vector<float> want(n, 0.0f), floorV(n, 0.0f);
                for (size_t k = 0; k < n; ++k) {
                    const size_t idx = victims[k];
                    want[k] = vertical ? effectiveHeight(items[idx]) : effectiveWidth(items[idx]);
                    const float mn = vertical ? items[idx]->minHeight() : items[idx]->minWidth();
                    floorV[k] = (mn > 0.0f) ? mn : 0.0f;
                }

                std::vector<bool> frozen(n, false);
                float deficit = freeLen;
                for (size_t round = 0; round <= n; ++round) {
                    float weight = 0.0f;
                    size_t live = 0;
                    for (size_t k = 0; k < n; ++k)
                        if (!frozen[k]) { weight += want[k]; ++live; }
                    if (live == 0) break;

                    const bool  byWeight = (weight > 0.0f);
                    const float denom = byWeight ? weight : static_cast<float>(live);
                    float absorbed = 0.0f;
                    bool  hitFloor = false;

                    for (size_t k = 0; k < n; ++k) {
                        if (frozen[k]) continue;
                        float& acc = growExtra[victims[k]];
                        const float give = deficit * ((byWeight ? want[k] : 1.0f) / denom);
                        if (want[k] + acc + give <= floorV[k]) {
                            const float total = floorV[k] - want[k];
                            absorbed += total - acc;
                            acc = total;
                            frozen[k] = true;
                            hitFloor = true;
                        } else {
                            acc += give;
                            absorbed += give;
                        }
                    }
                    deficit -= absorbed;
                    if (!hitFloor || deficit >= 0.0f) break;
                }
                freeLen = 0.0f;
            }
        }
        else {
            freeLen = std::max(freeLen, 0.0f);
        }

        if (!expanders.empty()) {
            const float share = freeLen / static_cast<float>(expanders.size());
            float left = freeLen;
            for (size_t k = 0; k < expanders.size(); ++k) {
                const size_t idx = expanders[k];
                const float give = (k + 1 == expanders.size()) ? left : share;

                float target = std::max(give, 0.0f);
                const float mn = vertical ? items[idx]->minHeight() : items[idx]->minWidth();
                const float mx = vertical ? items[idx]->maxHeight() : items[idx]->maxWidth();
                if (mn > 0.0f) target = std::max(target, mn);
                if (mx > 0.0f) target = std::min(target, mx);

                const float natural = vertical ? effectiveHeight(items[idx])
                                               : effectiveWidth(items[idx]);
                growExtra[idx] = target - natural;
                left -= give;
            }
            if (left > 0.0f)
                mainAxisDistribution(left, items.size(), lead, extraGap);
        }
        else {
            mainAxisDistribution(freeLen, items.size(), lead, extraGap);
        }
        const float step = mainGap + extraGap;

        if (!vertical) {
            measuredH_.clear();
            for (size_t i = 0; i < items.size(); ++i) {
                Widget* c = items[i];
                if (!c->heightDependsOnWidth() || c->prefWidth() > 0.0f) continue;
                const float w = effectiveWidth(c) + growExtra[i];
                if (w <= 0.0f) continue;
                measuredH_[c] = c->hintHeightFor(w);
            }
        }

        const float scrollOffset = vertical ? scrollOffsetY_ : scrollOffsetX_;
        const float startCursor = (vertical ? cy : cx) + lead - scrollOffset;
        float cursor = startCursor;
        float maxCross = 0.0f;
        for (size_t i = 0; i < items.size(); ++i) {
            Widget* c = items[i];
            const Margin& m = c->margin();

            const float mainSpan = mainSize(c);
            const float mainGrow = growExtra[i];

            const float wantMain = vertical ? effectiveHeight(c) : effectiveWidth(c);
            float applied = 0.0f;
            if (mainGrow != 0.0f) {
                const float target = std::max(wantMain + mainGrow, 0.0f);
                if (vertical) LayoutAccess::SetActual(*c, c->width(), target);
                else          LayoutAccess::SetActual(*c, target, c->height());
                applied = (vertical ? c->height() : c->width()) - wantMain;
            }
            else if (wantMain > 0.0f) {
                if (vertical) LayoutAccess::SetActual(*c, c->width(), wantMain);
                else          LayoutAccess::SetActual(*c, wantMain, c->height());
                applied = (vertical ? c->height() : c->width()) - wantMain;
            }

            const float cStartM = crossStartMargin(c);
            const float cEndM   = crossEndMargin(c);
            const AlignItems align = effectiveAlign(c);
            float crossPos = 0.0f, crossSz = 0.0f;
            alignCrossAxis(align, crossLen, crossSize(c), crossPos, crossSz);

            const float mainPos = cursor + (vertical ? m.top : m.left);
            if (vertical) c->SetPosition(cx + crossPos + cStartM, mainPos);
            else          c->SetPosition(mainPos, cy + crossPos + cStartM);

            if (align == AlignItems::Stretch) {
                const float wanted = std::max(crossLen - cStartM - cEndM, 0.0f);
                if (wanted != crossSize(c)) setCrossSize(c, wanted);
            }
            const float actualCross = (align == AlignItems::Stretch)
                ? actualCrossSize(c) : crossSz;

            maxCross = std::max(maxCross, crossPos + cStartM + actualCross + cEndM);
            cursor += mainSpan + applied + step;
        }

        const float usedMain = items.empty() ? 0.0f : (cursor - step - startCursor);
        contentW_ = vertical ? maxCross : usedMain;
        contentH_ = vertical ? usedMain : maxCross;
    }

    void LayoutBase::layoutFlow() {
        const std::vector<Widget*> items = visibleChildren();

        for (Widget* it : items) LayoutAccess::RestorePref(*it);

        measureWidthDependentHeights();

        const float availW = contentAvailableW();
        const float cx = contentLeft();
        const float cy = contentTop();
        const float colGap = style_.columnGap;
        const float rowGap = style_.rowGap;

        struct Row {
            std::vector<Widget*> items;
            float width = 0.0f;
            float height = 0.0f;
        };

        std::vector<Row> rows;
        Row cur;
        for (Widget* it : items) {
            const Margin& m = it->margin();
            const float iw = std::max(effectiveWidth(it) + m.left + m.right, 0.0f);
            const float gap = cur.items.empty() ? 0.0f : colGap;
            if (!cur.items.empty() && cur.width + gap + iw > availW) {
                rows.push_back(cur);
                cur = Row{};
            }
            cur.width += (cur.items.empty() ? 0.0f : colGap) + iw;
            cur.height = std::max(cur.height, effectiveHeight(it) + m.top + m.bottom);
            cur.items.push_back(it);
        }
        if (!cur.items.empty()) rows.push_back(cur);

        float cursorY = cy - scrollOffsetY_;
        float maxRowW = 0.0f;
        float rowsH = 0.0f;
        for (const Row& row : rows) {
            maxRowW = std::max(maxRowW, row.width);
            float cursorX = cx - scrollOffsetX_;
            for (Widget* it : row.items) {
                const Margin& m = it->margin();
                const float iw = effectiveWidth(it);
                const float ih = effectiveHeight(it);
                const float iy = cursorY + m.top
                    + (row.height - ih - m.top - m.bottom) * 0.5f;
                if (iw != it->width())  LayoutAccess::SetActual(*it, iw, it->height());
                if (ih != it->height()) LayoutAccess::SetActual(*it, it->width(), ih);
                it->SetPosition(cursorX + m.left, iy);
                cursorX += iw + m.left + m.right + colGap;
            }
            cursorY += row.height + rowGap;
        }

        if (!rows.empty()) rowsH = cursorY - cy - rowGap;

        contentW_ = std::max(maxRowW, availW);
        contentH_ = std::max(rowsH, 0.0f);
    }

}
