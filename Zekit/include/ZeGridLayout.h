#pragma once
#include "ZeLayoutBase.h"

// Grid layout: equal-width columns, equal height within a row, filled row by row from top to bottom
//
// WARNING: This layout only reads these styles: column count / columnGap / rowGap / padding.
//    The following have no effect on Grid (setting them does nothing, it is not a bug):
//      - justifyContent -- column width always fills the content width, no remaining space to distribute;
//      - alignItems / alignSelf -- items within a row are always "top-aligned + keep their own height";
//      - flexGrow -- does not participate in space distribution;
//      - margin -- not read, item spacing is controlled uniformly by columnGap / rowGap.

namespace Kit {
    class GridLayout : public LayoutBase {
    public:
        GridLayout();

        /// @brief Sets the column count (items fill row by row in add order)
        /// @param columns Column count (treated as 1 when < 1)
        void SetColumns(int columns);
        int columns() const { return columns_; }

        void DoLayout() override;
        void Paint(FrameCollector& fc) const override;

    private:
        int columns_ = 1;
    };
}
