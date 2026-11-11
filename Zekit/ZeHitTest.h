/**
 * @file ZeHitTest.h
 * @brief Hit test -- stateless pure algorithm that answers "which widget does this point hit"
 *
 * @details
 * This file holds no state, only a few free functions. This is possible because
 * everything the hit test needs lives on Widget (geometry x/y/w/h, visible_, children_),
 * and the algorithm itself only does "given a point, walk the hierarchy in reverse order to find the topmost one".
 *
 * Hierarchy rules (strictly aligned with paint order):
 *  1. Child widgets are above the parent -- recurse into children first; only if no child hits does the parent itself get considered;
 *  2. Later-added widgets are above earlier-added ones -- every level is traversed in reverse order (paint draws front to back);
 *  3. Invisible widgets (isVisible() == false) skip the entire subtree;
 *  4. Areas clipped away by a container do not participate in hit testing -- when a child crosses the parent's clipChildrenRect (scrolled out of
 *     the viewport, cut off by the clip region), even if the point falls inside its rect, it does not count as a hit.
 *     This rule is strictly aligned with paint: what is not visible should not be clickable, otherwise you get
 *     "click one widget, but the event goes to another invisible widget";
 *  5. Once a widget itself is hit, its children are returned first; only if no child hits is the widget itself returned.
 *
 * @note Boundary of rule 4: `clipChildrenRect` constrains child items and does not affect the widget's own
 *       hittable area -- the part of the container itself that is clipped away is still clickable (its own hit area is
 *       determined by HitTest).
 *
 * @note Coordinates are absolute client-area coordinates, consistent with MouseEvent::x / y.
 *       Current widget geometry is already absolute (no parent offset), so no conversion is needed between parent and child.
 *       If relative coordinates are introduced in the future, only one offset add/subtract is needed in this file's traversal.
 *
 * @see ZeWidget.h  Widget::HitTest (the widget's "hittable area" customization hook)
 * @see Zekit.h     Window (the only caller)
 */

#pragma once
#include <cstddef>
#include <span>
#include <vector>

#include "ZeWidget.h"

namespace Kit {
    namespace HitTest {

        // -------------------------------------------------------------------
        //  Internal implementation: recursion and reverse order
        // -------------------------------------------------------------------

        /// @brief Reverse-order recursive lookup (needs access to Widget's hierarchy to call HitTest before returning)
        /// @note Obtains children() via friend struct HitTestAccess, without exposing extra interfaces.
        namespace Detail {

            /// @brief Finds the hit target within a single widget and its subtree (children first, reverse order within a level)
            /// @param w Which widget to start searching downward from
            /// @param x Client-area X
            /// @param y Client-area Y
            /// @return The topmost hit widget; nullptr if nothing is hit
            /// @note Before entering children, ask the parent container for its "child clip region": when the point falls outside the clip region, the entire
            ///       subtree does not participate in hit testing -- aligned with paint (what is not visible should not be clickable).
            inline Widget* FindInSubtree(Widget* w, float x, float y) {
                if (!w || !w->isVisible()) return nullptr;

                // The parent container's child clip region (children scrolled out by ScrollView, parts outside the layout content area)
                float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
                const bool clips = w->clipChildrenRect(cx, cy, cw, ch);
                // Half-open interval, same convention as Widget::HitTest: adjacent edges belong only to the former
                const bool inClip = !clips ||
                    (x >= cx && x < cx + cw && y >= cy && y < cy + ch);

                if (inClip) {
                    // Check children first (children above, later-added above -> reverse order)
                    const std::vector<Widget*>& kids = w->children();
                    for (std::size_t i = kids.size(); i > 0; --i) {
                        if (Widget* hit = FindInSubtree(kids[i - 1], x, y))
                            return hit;
                    }
                }

                // If no child hits (or they are blocked by the clip region), then ask itself.
                // Note: this line is outside inClip -- the clip region only governs children, not itself.
                return w->HitTest(x, y) ? w : nullptr;
            }

            /// @brief Finds the hit target among overlays (paintsOnTop) within a single widget and its subtree
            /// @param w   Which widget to start searching downward from
            /// @param x,y Client-area coordinates
            /// @return The topmost hit overlay; nullptr if no overlay in that subtree is hit
            ///
            /// @details
            /// Two differences from `FindInSubtree`, both arising from the "overlay" identity:
            ///
            ///  1. Only descends along directions that contain overlay descendants -- ordinary subtrees are skipped entirely. So
            ///     the determination of "does this level have an overlay" is naturally done by the recursive structure, without an extra
            ///     `containsOverlay()` pre-scan (which would be O(n^2)).
            ///  2. Does not apply the ancestor's clip region -- the overlay escapes the container's clip region in its own `Paint` via
            ///     `fc.ResetClip()`, so the hit test must escape along with it,
            ///     otherwise you get "revealed but not clickable" (a worse failure mode than "clipped away").
            ///     The overlay's own clip region still applies (asking it in `FindInSubtree` is correct).
            ///
            /// @note Order is strictly aligned with paint: the second paint pass draws each overlay
            ///       subtree in registration order (tree preorder), so here we traverse in reverse order -- later-painted (higher) ones are returned first.
            inline Widget* FindInOverlay(Widget* w, float x, float y) {
                if (!w || !w->isVisible()) return nullptr;

                // It is an overlay -> search the entire subtree in the usual order (hierarchy inside the overlay still holds)
                if (w->paintsOnTop()) return FindInSubtree(w, x, y);

                // Otherwise only search among children (reverse order = higher in paint order first)
                const std::vector<Widget*>& kids = w->children();
                for (std::size_t i = kids.size(); i > 0; --i) {
                    if (Widget* hit = FindInOverlay(kids[i - 1], x, y))
                        return hit;
                }
                return nullptr;
            }

        } // namespace Detail

        // -------------------------------------------------------------------
        //  Public interface
        // -------------------------------------------------------------------

        /**
         * @brief Finds the topmost widget hit by the point among a set of top-level widgets
         * @param roots Top-level widget sequence (order = paint order, the last one is painted on top)
         * @param x     Client-area X coordinate (pixels)
         * @param y     Client-area Y coordinate (pixels)
         * @return The topmost hit widget; nullptr when the point falls outside all widgets
         *
         * @details
         * Traverses roots in reverse order (later-added first), recursing into each root's subtree.
         * Two passes: first check overlays (`paintsOnTop()` subtrees), then ordinary widgets -- strictly aligned with the
         * "two-pass paint", ensuring "what is painted on top also receives the click first".
         *
         * @par Usage
         * @code
         * if (Widget* target = Kit::HitTest::Topmost(winWidgets, mx, my)) {
         *     MouseEvent e{ ... };
         *     while (target && !target->OnMousePress(e)) target = target->parent();
         * }
         * @endcode
         */
        inline Widget* Topmost(std::span<Widget* const> roots, float x, float y) {
            // * Overlays first: in paint order overlays are always drawn last (second paint pass), so the hit test must check them first.
            //   Order is likewise reverse -- later-painted is on top. (For UIs without overlays this pass is all nullptr,
            //   bit-for-bit equivalent to "only one pass".)
            for (std::size_t i = roots.size(); i > 0; --i) {
                if (Widget* hit = Detail::FindInOverlay(roots[i - 1], x, y))
                    return hit;
            }
            for (std::size_t i = roots.size(); i > 0; --i) {
                if (Widget* hit = Detail::FindInSubtree(roots[i - 1], x, y))
                    return hit;
            }
            return nullptr;
        }

        /**
         * @brief Finds the topmost widget hit by the point within a single widget's subtree (including the widget itself)
         * @param root Subtree root
         * @param x    Client-area X coordinate (pixels)
         * @param y    Client-area Y coordinate (pixels)
         * @return The topmost hit widget; nullptr if nothing is hit
         *
         * @note Overlays (widgets whose `paintsOnTop()` returns true, such as an expanded dropdown) are
         *       automatically prioritized by this function: it searches overlay subtrees first, and only checks ordinary widgets if none hit -- strictly aligned with the
         *       "two-pass paint", so callers do not need to query twice.
         */
        inline Widget* Topmost(Widget& root, float x, float y) {
            if (Widget* hit = Detail::FindInOverlay(&root, x, y))
                return hit;
            return Detail::FindInSubtree(&root, x, y);
        }

    } // namespace HitTest
} // namespace Kit
