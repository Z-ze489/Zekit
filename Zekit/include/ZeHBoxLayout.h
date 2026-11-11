#pragma once
#include "ZeLayoutBase.h"

// Horizontal layout: children are arranged in a single row from left to right

namespace Kit {
    class HBoxLayout : public LayoutBase {
    public:
        HBoxLayout();

        void DoLayout() override;
        void Paint(FrameCollector& fc) const override;
    };
}
