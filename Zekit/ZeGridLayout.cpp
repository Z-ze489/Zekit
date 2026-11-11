#include "pch.h"
#include "include/ZeGridLayout.h"

#include "ZeFrameCollector.h"

#include <algorithm>

namespace Kit {

    GridLayout::GridLayout() {
        style_.direction = FlexDirection::Row;
    }

    void GridLayout::SetColumns(int columns) {
        const int v = (columns < 1) ? 1 : columns;
        if (columns_ == v) return;
        columns_ = v;
        Invalidate();
        RequestWindowRedraw();
    }

    void GridLayout::DoLayout() {
        const std::vector<Widget*> items = visibleChildren();

        for (Widget* it : items) LayoutAccess::RestorePref(*it);

        const float availW = contentAvailableW();
        const float cx = contentLeft();
        const float cy = contentTop();

        const int cols = columns_;
        const float colGap = style_.columnGap;
        const float rowGap = style_.rowGap;

        const float gaps = colGap * static_cast<float>(cols - 1);
        const float cellW = std::max((availW - gaps) / static_cast<float>(cols), 0.0f);

        float cursorY = cy;
        size_t k = 0;
        while (k < items.size()) {
            const size_t rowN = std::min<size_t>(
                static_cast<size_t>(cols), items.size() - k);

            float rowH = 0.0f;
            for (size_t j = 0; j < rowN; ++j) {
                rowH = std::max(rowH, effectiveHeight(items[k + j]));
            }

            for (size_t j = 0; j < rowN; ++j) {
                Widget* it = items[k + j];
                const float ix = cx + static_cast<float>(j) * (cellW + colGap);
                it->SetPosition(ix, cursorY);
                LayoutAccess::SetActual(*it, cellW, effectiveHeight(it));
            }

            cursorY += rowH + rowGap;
            k += rowN;
        }

        contentW_ = availW;
        contentH_ = items.empty() ? 0.0f : cursorY - cy - rowGap;
    }

    void GridLayout::Paint(FrameCollector& fc) const {
        (void)fc;
    }

}
