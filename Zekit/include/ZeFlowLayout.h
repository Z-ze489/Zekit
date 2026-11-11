#pragma once
#include "ZeLayoutBase.h"

// Flow layout: arranges horizontally, wraps automatically when items do not fit (short items in a row are vertically centered)
//
// WARNING: This layout only reads rowGap / columnGap / margin / padding, and items in a row are fixed
//    to left alignment + vertical centering. The following have no effect (setting them does nothing, it is not broken):
//      - justifyContent -- rows are always left-aligned;
//      - alignItems / alignSelf -- items in a row are always vertically centered;
//      - flexGrow -- does not participate in space distribution.

namespace Kit {
    class FlowLayout : public LayoutBase {
    public:
        FlowLayout();

        void DoLayout() override;
        void Paint(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Measurement protocol override: the height of Flow depends on how wide it is given
        // -------------------------------------------------------------------
        /**
         * @brief Returns the height this layout needs after wrapping at the given width
         * @param availWidth content width the parent container intends to give this layout (excluding this layout's padding)
         * @return height needed after wrapping (including top and bottom padding)
         *
         * @details
         * This is the key method Flow must implement itself -- it is the only layout whose height depends on width:
         * width determines how many items fit per row -> determines row count -> determines total height. It is computed with a read-only trial pass
         * (does not modify any child geometry, only accumulates row heights).
         *
         * WARNING: The trial pass must use `effectiveWidth/Height` (use pref if pref>0, otherwise query hint),
         *    sharing the same grouping algorithm as in `DoLayout`; otherwise the measured height and the actually laid-out height
         *    will disagree (measure 3 rows, lay out 4 rows).
         */
        float hintHeightFor(float availWidth) const override;

        /// @brief Always true -- the height of Flow depends on width, so the parent container must fix the width before querying height
        bool heightDependsOnWidth() const override { return true; }
    };
}
