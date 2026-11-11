#include "pch.h"
#include "include/ZeHBoxLayout.h"

#include "ZeFrameCollector.h"

namespace Kit {

    HBoxLayout::HBoxLayout() {
        style_.direction = FlexDirection::Row;
    }

    void HBoxLayout::DoLayout() {
        layoutLinear(false);
    }

    void HBoxLayout::Paint(FrameCollector& fc) const {
        (void)fc;
    }

}
