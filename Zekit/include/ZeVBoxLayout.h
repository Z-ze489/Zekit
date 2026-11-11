#pragma once
#include "ZeLayoutBase.h"

// Vertical layout: children stacked top to bottom in a single column

namespace Kit {
    class VBoxLayout : public LayoutBase {
    public:
        VBoxLayout();

        void DoLayout() override;
        void Paint(FrameCollector& fc) const override;
    };
}
