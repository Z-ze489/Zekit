/**
 * @file ZeFlexStyle.h
 * @brief Flex layout style -- the rules for how a container arranges its child widgets
 *
 * @details
 * All layout containers (VBox / HBox / Grid / ScrollContent) share this file.
 *
 * Boundaries (important, earlier versions got this wrong):
 *  - This file holds only container properties -- it describes how the parent container arranges child widgets;
 *  - It does not hold child item properties -- alignSelf / flexGrow describe how this child widget wants to be arranged,
 *    they belong to Widget itself, not to the style module, and certainly not here;
 *  - It does not hold display / visibility / overflow -- those are the Widget's
 *    layout switches and render pipeline capabilities, not style fields.
 *
 * One-sentence criterion:
 *  "Does this field describe the container as a whole, or one particular child widget?"
 *   Whole -> goes into FlexStyle; single -> goes into Widget.
 *
 * @see ZeBoxStyle.h box style
 * @see ZeTextStyle.h text style
 */

#pragma once

namespace Kit {

    // ===================================================================
    //  Enums: pick one from a few fixed options
    // ===================================================================

    /// @brief Main axis direction
    /// @note One direction only. Reverse ordering is expressed by "changing the order in which child items are added" -- reverse flow semantics are convoluted
    ///       and almost never used in UIs; the previously reserved Reverse value was never implemented and has been removed.
    enum class FlexDirection {
        Row,            /// < Horizontal: left to right
        Column          /// < Vertical: top to bottom
    };

    /// @brief Main axis alignment (how remaining space is distributed along the direction)
    enum class JustifyContent {
        Start,          /// < Align to start
        End,            /// < Align to end
        Center,         /// < Center
        SpaceBetween,   /// < Space between, evenly distributed in the middle
        SpaceAround,    /// < Half spacing on each side of every item
        SpaceEvenly     /// < Evenly distribute all spacing (including both ends)
    };

    /// @brief Cross axis alignment (how to align perpendicular to the direction)
    enum class AlignItems {
        Start,      /// < Align to start
        End,        /// < Align to end
        Center,     /// < Center
        Stretch,    /// < Stretch to fill the cross axis
        Baseline    /// < Align by text baseline
    };

    // ===================================================================
    //  Main structure: container flex layout style
    // ===================================================================

    /**
     * @brief Flex layout style
     *
     * @details
     * A layout container composes this structure to gain full one-dimensional flex layout capability.
     * All fields are container-level -- all child items in the same container share the same set of rules;
     * if an individual child item wants to override, use the child item properties on Widget (not in this file).
     *
     * Default construction = horizontal arrangement, start alignment, no spacing.
     *
     * @note About "wrapping" and "multi-line alignment": this structure deliberately has no such fields.
     *       Wrapping is inherent behavior of a specific container (FlowLayout / ScrollView bidirectional), not
     *       a tunable switch -- for single-line horizontal arrangement use HBoxLayout directly, no need to add a switch to the flow layout
     *       (one thing has only one expression in the whole library). Alignment between multiple lines is not yet implemented;
     *       add fields when there is a real need, do not leave fake fields that "do nothing when set".
     */
    struct FlexStyle {
        // ---- Main axis ----
        FlexDirection direction = FlexDirection::Row;

        // ---- Alignment ----
        JustifyContent justifyContent = JustifyContent::Start;   /// < Main axis alignment
        AlignItems     alignItems = AlignItems::Stretch;     /// < Cross axis alignment

        // ---- Spacing ----
        float rowGap = 0.0f;   /// < Row spacing (effective for FlowLayout / multiple lines)
        float columnGap = 0.0f;   /// < Column spacing

        // ---- Convenience read/write ----
        // @brief Sets row and column spacing at once (rowGap = columnGap = g)
        void SetGap(float g) { rowGap = columnGap = g; }
    };

} // namespace Kit