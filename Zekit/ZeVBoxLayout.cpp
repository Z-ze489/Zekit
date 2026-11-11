#include "pch.h"
#include "include/ZeVBoxLayout.h"

#include "ZeFrameCollector.h"

namespace Kit {

    VBoxLayout::VBoxLayout() {
        style_.direction = FlexDirection::Column;
    }

    void VBoxLayout::DoLayout() {
        layoutLinear(true);
    }

    void VBoxLayout::Paint(FrameCollector& fc) const {
        (void)fc;
    }

}
