/**
 * @file ZeSteps.h
 * @brief Steps: `①──②──③` a progress flow indicator
 *
 * @details
 * `Steps : ItemStripBase` -- item model, hover / click / keyboard navigation,
 * "paint and hit test share one geometry" are all inherited from the base class. This class only adds the four things specific to a steps bar:
 *
 *  1. Circle: one circle per step, with the index inside; completed steps paint a check mark (`✓`);
 *  2. Connector line: a horizontal line between steps, completed segments use the primary color, incomplete segments use the separator color
 *     -- so "how far along" is visible at a glance, without looking only at the highlighted circle;
 *  3. State: `completed (index < current)` / `in progress (== current)` / `pending (> current)`;
 *  4. Equal width: each step occupies the same width (`available width / step count`), so connector lengths are uniform and the whole is aligned
 *     -- unlike breadcrumbs / pagination which size by content width, this is an inherent layout requirement of the steps bar.
 *
 * @par Usage
 * @code
 * Kit::Steps steps;
 * steps.AddStep("Fill in info");
 * steps.AddStep("Confirm order");
 * steps.AddStep("Complete payment");
 * steps.SetCurrentStep(1);                       // step 2 in progress
 * steps.SetOnStepChanged([](Kit::Widget& self, int i) {
 *     std::printf("switched to step %d\n", i);    // 0-based
 * });
 * win.AddWidget(steps);
 * @endcode
 *
 * @note "User clicks / arrow keys switch steps" and "programmatic `SetCurrentStep`" are two paths:
 *       only the former triggers the callback -- same rule as `TabView` / `ComboBox` / `Pagination`.
 * @note Clicking a completed step is allowed (going back is a common need); to forbid going back, ignore it
 *       yourself in the callback -- the library does not decide flow policy for the business.
 *
 * @see ZeItemStripBase.h  common base class in the library (geometry / state / input)
 * @see ZePagination.h     another "row of discrete items" widget in the same family
 */

#pragma once
#include "../ZeItemStripBase.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Steps bar
     *
     * @details
     * Theme uses the `itemStrip` section: `itemActiveColor` is the "completed / in-progress circle fill",
     * `itemActive` controls the current step text, `item` controls the remaining text, `separatorColor` controls the incomplete connector lines.
     */
    class Steps : public ItemStripBase {
    public:
        Steps();

        // -------------------------------------------------------------------
        //  Steps (data)
        // -------------------------------------------------------------------

        /// @brief Appends a step
        /// @param title Name of this step
        /// @return Index of the new step (0-based)
        int AddStep(std::string title);

        /// @brief Sets all steps at once
        void SetSteps(const std::vector<std::string>& titles);

        /// @brief Renames a step (out of range is silently ignored)
        void SetStepTitle(int index, std::string title);
        /// @brief Returns the name of a step (out of range returns an empty string)
        const std::string& stepTitle(int index) const { return itemLabel(index); }

        /// @brief Step count
        std::size_t stepCount() const { return itemCount(); }

        // -------------------------------------------------------------------
        //  Current step (still 0-based externally -- a steps bar is naturally 0-based, unlike pagination which has a "page number")
        // -------------------------------------------------------------------

        /// @brief Sets the current step programmatically (does not trigger the callback; out of range is silently ignored)
        void SetCurrentStep(int index) { SetCurrentIndex(index);  RequestWindowRedraw();}
        /// @brief Current step index (-1 when there are no steps)
        int currentStep() const { return currentIndex(); }

        /// @brief Whether a step is completed (index < current step)
        bool isStepDone(int index) const {
            return index >= 0 && index < currentIndex();
        }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Circle diameter (pixels; 0 = follow theme `itemStrip.itemHeight`)
        void SetCircleSize(float diameter);
        /// @brief Current circle diameter
        float circleSize() const { return circleSize_; }

        /// @brief Horizontal spacing between circle and name (pixels)
        void SetTitleGap(float gap);
        /// @brief Current spacing
        float titleGap() const { return titleGap_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the "user switched step" callback (with sender + new step index)
        void SetOnStepChanged(SelfAction1<int> cb) { onStepChanged_ = std::move(cb); }
        /// @brief Sets the "user switched step" callback (form that only cares about the sender)
        void SetOnStepChanged(SelfAction cb) { onStepChangedSelf_ = std::move(cb); }

    protected:
        // -------------------------------------------------------------------
        //  Base class hooks
        // -------------------------------------------------------------------

        /// @brief Equal width: each step occupies `available width / step count` (with a lower bound, so it never squeezes to 0 when too narrow)
        float measureStripItem(int index) const override;

        /// @brief Connector line width (= circle radius; the line is drawn through the circle center, and the circle covers both ends)
        float separatorWidth() const override;
        /// @brief Paints the connector lines (completed segments primary color / incomplete segments separator color)
        void paintStripSeparator(FrameCollector& fc, const ItemRect& r) const override;

        /// @brief Paints one step: circle (index / check mark) + name
        void paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const override;

        /// @brief User switched step -> converted into a callback
        void OnCurrentChangedByUser(int index) override;

    private:
        /// @brief Placement of the circle within the current item (shared by paint and measure)
        void circleRect(const ItemRect& r, float& cx, float& cy, float& d) const;

        /// @brief Circle diameter (with fallback)
        float resolvedCircle() const;

        float circleSize_ = 0.0f;   /// < 0 = follow theme
        float titleGap_ = 8.0f;

        // ---- Callbacks ----
        SelfAction1<int> onStepChanged_;
        SelfAction       onStepChangedSelf_;
    };

} // namespace Kit
