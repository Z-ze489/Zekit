/**
 * @file ZeAnchorVisible.h
 * @brief **Library internal**: the single criterion for whether a popup anchor is "still somewhere visible"
 *
 * @details
 * The four widgets that carry a popup (`ComboBox` / `TimePicker` / `DateTimePicker` / `ColorPicker`)
 * each implemented `popupAnchorVisible()` once, and **the semantics have already diverged**:
 *
 * | Implementation | Start point | Checks `isVisible()` | When an ancestor does not clip children |
 * |---|---|---|---|
 * | ComboBox / TimePicker | `parent()` | ❌ not checked | `continue` (does not count) |
 * | ColorPicker / DateTimePicker | `this` | ✅ checked | `continue` |
 *
 * The difference is not theoretical:
 *  · Not checking visibility -> after the anchor calls `SetVisible(false)` the popup **is still up** ("an invisible widget still has its popup open");
 *  · Starting from `parent()` -> when the anchor itself is hidden the check is likewise missed.
 *
 * So it is unified to "start from self, check visibility layer by layer; ask only **ancestors** about the clip region". This is both the
 * union of the two implementations and the only self-consistent version: self is not clipped by self, and an ancestor that does not clip children does not constitute
 * occlusion at that layer (for example `Panel` does not clip by default).
 *
 * @note Coordinates are always **window absolute coordinates** (that is the convention for widget geometry in this library), so taking
 *       `self.x()/y()` and comparing directly with an ancestor's clip rectangle is enough; no per-layer conversion is needed.
 * @see Widget::clipChildrenRect  public query point for an ancestor's clip region
 * @see ZeOverlayWindow           native carrier (its `SupportsNative` only covers platform capability;
 *                                visibility is handled by this criterion)
 */

#pragma once

#include "include/ZeWidget.h"

namespace Kit {
    namespace detail {

        /**
         * @brief Whether the anchor (and its ancestor chain) is still visible at this moment
         * @param self The anchor widget (usually the widget the popup is attached to)
         * @return false = self or some ancestor is hidden / an ancestor has clipped away the whole area -- the popup should not be shown in that case
         *
         * @details
         * The criterion does two things per layer:
         *  1. This layer's `isVisible()` is false -> not visible (a widget in a hidden subtree should not pop up);
         *  2. This layer clips children (`clipChildrenRect` returns true) -> the anchor rectangle must intersect the
         *     clip rectangle, otherwise it is blocked outside (typical: scrolled out of a `ScrollView` viewport).
         *
         * @note **Ancestors that do not clip children are skipped directly** (`Panel` / ordinary containers are like this) --
         *       treating "does not clip" as "clips to empty" would make every popup refuse to open.
         */
        inline bool AnchorVisibleInAncestors(const Widget& self) {
            const float ax = self.x();
            const float ay = self.y();
            const float aw = self.width();
            const float ah = self.height();

            for (const Widget* p = &self; p; p = p->parent()) {
                if (!p->isVisible()) return false;

                // The self layer does not take part in the clip check (that is "I clip my children", unrelated to whether I am visible)
                if (p == &self) continue;

                float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
                if (!p->clipChildrenRect(cx, cy, cw, ch)) continue;   // Does not clip children: does not constitute occlusion

                if (ax + aw <= cx || ax >= cx + cw) return false;
                if (ay + ah <= cy || ay >= cy + ch) return false;
            }
            return true;
        }

    } // namespace detail
} // namespace Kit
