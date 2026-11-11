#include "pch.h"
#include "include/ZeFlowLayout.h"

#include "ZeFrameCollector.h"

namespace Kit {

    FlowLayout::FlowLayout() {
        style_.direction = FlexDirection::Row;
    }

    void FlowLayout::DoLayout() {
        layoutFlow();
    }

    float FlowLayout::hintHeightFor(float availWidth) const {
        const std::vector<Widget*> items = visibleChildren();

        const float innerW = std::max(availWidth - padding().left - padding().right, 0.0f);

        const float colGap = style_.columnGap;
        const float rowGap = style_.rowGap;

        float totalH = 0.0f;
        float rowW = 0.0f;
        float rowH = 0.0f;
        int   rows = 0;

        for (Widget* it : items) {
            const Margin& m = it->margin();
            const float iw = std::max(effectiveWidth(it) + m.left + m.right, 0.0f);
            const float ih = effectiveHeight(it) + m.top + m.bottom;

            if (rowW > 0.0f && rowW + colGap + iw > innerW) {
                totalH += rowH + rowGap;
                rowW = 0.0f;
                rowH = 0.0f;
                ++rows;
            }

            rowW += (rowW > 0.0f ? colGap : 0.0f) + iw;
            rowH = std::max(rowH, ih);
        }

        if (rowW > 0.0f) { totalH += rowH; ++rows; }

        return totalH + padding().top + padding().bottom;
    }

    void FlowLayout::Paint(FrameCollector& fc) const {
        (void)fc;
    }

}
