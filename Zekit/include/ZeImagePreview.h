/**
 * @file ZeImagePreview.h
 * @brief Kit::ImagePreview -- in-window overlay image viewer (zoom / pan / multi-image switching)
 *
 * @details
 * `ImagePreview : BoxWidget` -- it has NO "collapsed face": when closed it is nothing
 * (takes no layout, paints nothing, does not hit), and only after `Show(path)` does it become a layer covering the whole client area: a dark
 * mask + a centered large image + top/bottom info bars.
 *
 * Division of labor with `Image`: `Image` is a static thumbnail placed into the layout (fits one image
 * into a rectangle according to `BgImageDrawMode`); this widget is a viewer covering the whole window -- wheel zoom, drag pan,
 * double-click toggles between "fit window / 1:1", left/right to page through multiple images. The two are often used together: put a bunch of
 * `Image` in a grid, and whichever is clicked is fed to the viewer.
 *
 * @par Overlay in three halves + modal capture (two existing mechanisms, zero new library capability)
 *  1. Paint: `paintsOnTop()` returns true when open -> deferred to the second paint pass, always above
 *     ordinary widgets; `paintContent()` calls `ResetClip()` at the start to escape the ancestor clip region,
 *     so a viewer mounted in a scroll area / split pane can also cover the whole window;
 *  2. Hit: when open, `HitTest` returns true for the whole client area -- this is modal capture
 *     (the same mechanism as `MenuModal`, not a separate window): while open, the widgets underneath receive no
 *     stateful input, and the wheel does not scroll the background;
 *  3. Keyboard: requests focus when open (`Esc` / arrow keys available immediately), returns it when closed.
 *
 * @par Basic usage
 * @code
 * Kit::ImagePreview viewer;
 * viewer.SetOnClosed([](Kit::Widget&) { /​* preview closed *​/ });
 * win.AddWidget(viewer);                        // * mount at top level (pure overlay)
 *
 * // (1) view a single image
 * viewer.Show("D:/photos/a.jpg");
 *
 * // (2) view a group (starting from the 3rd), left/right keys / side arrows page
 * viewer.Show({ "a.jpg", "b.jpg", "c.jpg" }, 2);
 *
 * // (3) open from a thumbnail -- one line in any widget's callback (same idiom as ZeMenuModal)
 * thumb.SetOnMousePress([&](Kit::Widget&, Kit::MouseEvent& e) {
 *     if (e.button == Kit::MouseButton::Left) viewer.Show("a.jpg");
 *     return false;                             // return false: do not swallow the event
 * });
 * @endcode
 *
 * @par Interaction overview
 * | Operation | Behavior |
 * |---|---|
 * | Wheel | Zoom anchored at the cursor (the point of the image under the pointer stays fixed) |
 * | Left drag | Pan (only an image larger than the viewport can be dragged, and it cannot be dragged past the bounds) |
 * | Double-click | Toggle between "fit window" and "1:1 actual pixels" |
 * | `Esc` | Close |
 * | `Left` / `Right` (`PageUp` / `PageDown`) | Previous / next (multi-image; by default no wrap at the ends) |
 * | `+` / `-` (`=` / `-` and numeric keypad) | Zoom in / out |
 * | `0` | Fit window |
 * | `1` | 1:1 actual pixels |
 * | Click the `X` at top right | Close |
 * | Click the left/right side arrows | Page (only when there are multiple images) |
 *
 * @note Pure overlay: takes no layout (`hintWidth` / `hintHeight` are always 0; collapsing to
 *       0 height in a VBox is exactly the expected behavior). The correct usage is `win.AddWidget(viewer)` to mount at top level.
 * @note Images are handed to the render layer by path (`FrameCollector::AddImage`); the widget itself does not load
 *       and does not touch GL -- the same pattern as the `Image` / `Button` icons.
 * @note The zoom factor records the factor "relative to fit window"; when the window size changes it re-fits
 *       automatically, until the user has zoomed themselves (then the user's factor is kept). This is the common feel of image viewers.
 *
 * @see ZeImage.h      static image placed into the layout
 * @see ZeMenuModal.h  also a "whole-window modal capture" overlay (the source of this widget's mechanism)
 * @see ZeTheme.h      `ImagePreviewStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeSelfAction.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Kit {

    class OverlayWindow;   /// < native overlay carrier (shared per layer, see ZeOverlayWindow.h)

    /**
     * @brief In-window overlay image viewer: zoom / pan / multi-image switching
     */
    class ImagePreview : public BoxWidget {
    public:
        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~ImagePreview() override;

        /// @brief Value semantics: moved along with AddWidget (an explicit destructor suppresses the implicit move, so it must be restored)
        ImagePreview(ImagePreview&&) noexcept;

        /// @brief Constructs (default style takes the built-in light theme; injected again after mounting to a window)
        ImagePreview();

        // -------------------------------------------------------------------
        //  Open / close
        // -------------------------------------------------------------------

        /// @brief Views one image (equivalent to `Show({path}, 0)`)
        /// @note An empty path is a silent no-op (does not open an empty preview)
        void Show(std::string path);

        /// @brief Views a group of images starting at `index` (out of range is clamped to the valid range)
        /// @param paths image path list (empty list = do not open)
        /// @param index start index (0-based)
        void Show(std::vector<std::string> paths, int index = 0);

        /// @brief Closes (idempotent; emits `OnClosed`)
        void Close();

        /// @brief Whether currently open
        bool isOpen() const { return open_; }

        // -------------------------------------------------------------------
        //  Data (calling while open takes effect immediately and resets the view)
        // -------------------------------------------------------------------

        /// @brief Replaces the whole image list (`index` out of range is clamped; empty list = stay open but with no image)
        void SetSources(std::vector<std::string> paths, int index = 0);

        /// @brief Appends one image (does not change the current index)
        void AddSource(std::string path);

        /// @brief Clears the image list (does NOT close the preview; the current image becomes empty)
        void Clear();

        /// @brief Current image list
        const std::vector<std::string>& sources() const { return sources_; }

        /// @brief Current index (returns -1 when there is no image)
        int index() const;

        /// @brief Switches to image index (out of range is clamped; resets to "fit window")
        void SetIndex(int index);

        /// @brief Next image (does not wrap when already at the last one, returns false)
        bool Next();

        /// @brief Previous image (does not wrap when already at the first one, returns false)
        bool Previous();

        /// @brief Current image path (empty string when there is no image)
        const std::string& source() const;

        /// @brief Number of images
        std::size_t count() const { return sources_.size(); }

        /// @brief Whether paging wraps at the ends (default false)
        void SetLoop(bool on);
        bool loop() const { return loop_; }

        // -------------------------------------------------------------------
        //  Zoom / pan
        // -------------------------------------------------------------------
        //  Factor semantics: 1 = fit window (whole image visible), not "1 image pixel = 1 screen pixel".
        //  1:1 actual pixels is converted by `ActualSize()` (it needs the original image size, so it may only be
        //  exact after the first frame -- before any paint it approximates "fit = 1").

        /// @brief Sets the zoom factor (1 = fit window; clamped into [minZoom, maxZoom])
        void SetZoom(float zoom);
        /// @brief Current zoom factor (1 = fit window)
        float zoom() const;

        /// @brief Fits the window (whole image visible, centered)
        void FitToWindow();
        /// @brief 1:1 actual pixels (one image pixel occupies one screen pixel)
        void ActualSize();
        /// @brief Zooms in one step (same step as the wheel)
        void ZoomIn();
        /// @brief Zooms out one step
        void ZoomOut();

        /// @brief Whether currently in "fit window" (recomputed automatically as the window size changes)
        bool isFitted() const { return fitted_; }

        /// @brief Zoom factor per wheel notch (default 1.15; clamped into [1.01, 3])
        void SetZoomStep(float step);
        float zoomStep() const { return zoomStep_; }

        /// @brief Zoom range (relative to the page upper bound, default [0.1, 20])
        void SetZoomRange(float minZoom, float maxZoom);
        float minZoom() const { return minZoom_; }
        float maxZoom() const { return maxZoom_; }

        /// @brief Resets the image position to centered (does not change the factor)
        void CenterImage();

        // -------------------------------------------------------------------
        //  Appearance / behavior
        // -------------------------------------------------------------------

        /// @brief Background mask color (default near-black translucent; alpha determines how much it dims)
        void SetScrimColor(const Color4f& c);
        const Color4f& scrimColor() const { return scrim_; }

        /// @brief Whether to show the top info bar (title + index + close button; shown by default)
        void SetShowTopBar(bool on);
        bool showsTopBar() const { return showTopBar_; }

        /// @brief Whether to show the bottom info bar (file name + factor + operation hints; shown by default)
        void SetShowBottomBar(bool on);
        bool showsBottomBar() const { return showBottomBar_; }

        /// @brief Whether to show the close button at top right (shown by default)
        void SetShowCloseButton(bool on);
        bool showsCloseButton() const { return showCloseButton_; }

        /// @brief Whether to show the left/right paging arrows (shown by default; actually painted only with multiple images)
        void SetShowNavArrows(bool on);
        bool showsNavArrows() const { return showNavArrows_; }

        /// @brief Title (empty = use the current file name)
        void SetTitle(std::string title);
        const std::string& title() const { return title_; }

        /// @brief Whether double-click toggling "fit / 1:1" is enabled (enabled by default)
        void SetDoubleClickTogglesFit(bool on);
        /// @brief Whether clicking empty background closes (default does NOT close -- a stray click while viewing a large image is annoying)
        void SetClickBackgroundCloses(bool on);

        /// @brief Close callback (lifecycle notification; emitted by both programmatic Close and user Esc / X)
        void SetOnClosed(SelfAction handler);

        /// @brief Image-change callback (emitted by user paging / `SetIndex`; `index` is 0-based)
        using IndexAction = SelfAction1<int>;
        /// @brief Sets the image-change callback (sender is this viewer; the argument is the new index)
        void SetOnIndexChanged(IndexAction handler);

        // -------------------------------------------------------------------
        //  Measurement protocol (pure overlay takes no layout)
        // -------------------------------------------------------------------
        // @brief Returns the explicit SetSize if set, otherwise 0 (pure overlay takes no layout)
        float hintWidth() const override;
        /// @brief Same as hintWidth
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay / modal hit / animation
        // -------------------------------------------------------------------
        // @brief Returns true when open (or fading in) -- makes Window defer painting to the "top overlay phase"
        bool paintsOnTop() const override;
        /// @brief When open, the whole client area counts as its own hittable region (modal capture)
        bool HitTest(float x, float y) const override;
        /// @brief Fade-in not finished -> needs another frame
        bool wantsAnimationFrame() const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------
        // @brief Re-injects mask / info bars / title / caption / widget glyph colors from the theme
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;
        Cursor cursorAt(float x, float y) const override;

    protected:
        /// @brief Default box style takes the theme's imagePreview section (top/bottom info bars)
        const BoxStyle& themeBox(const Theme& t) const override;

        /// @brief Fully takes over the base class paint template: paints nothing when closed (same as MenuModal)
        void Paint(FrameCollector& fc) const override;
        /// @brief Paints mask + image + top/bottom info bars + close button + paging arrows
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief All rectangles computed by one layout pass (shared by paint and hit)
        struct Geom {
            float winW = 0.0f, winH = 0.0f;      /// < client area size
            float topH = 0.0f, botH = 0.0f;      /// < top / bottom bar height (0 = not shown)
            float vx = 0.0f, vy = 0.0f;          /// < image available viewport (relative to the client area)
            float vw = 0.0f, vh = 0.0f;
            float ix = 0.0f, iy = 0.0f;          /// < image draw rectangle
            float iw = 0.0f, ih = 0.0f;
            float closeX = 0.0f, closeY = 0.0f, closeS = 0.0f;   /// < close button (top-left + side length)
            float navLx = 0.0f, navRx = 0.0f;    /// < left/right paging button center X
            float navY = 0.0f, navS = 0.0f;      /// < paging button center Y + size
            bool  hasNav = false;                /// < whether multi-image paging is available
            bool  valid = false;
        };

        /// @brief Computes geometry (pure query, writes no state; relies on the cached original image size)
        Geom computeGeom() const;

        // -------------------------------------------------------------------
        //  Native overlay carrier (OverlayWindow path; always empty on fallback platforms)
        // -------------------------------------------------------------------

        /// @brief Whether the overlay is currently carried by the native carrier (a whole-window-covering Popup window)
        bool nativePopup() const;
        /// @brief Syncs the carrier to the host client area (hidden when closed; repaint only when the size is unchanged)
        void syncOverlay() const;

        /// @brief Original image size cache (0 = not yet known, filled by `ImageSize` on first paint)
        void ensureImageSize(FrameCollector& fc) const;

        /// @brief Fit factor (whole image into the viewport; returns 0 when the size is unknown)
        float fitScale() const;

        /// @brief Clamps the current factor / offset back into the valid range (called after zooming)
        void clampView();

        /// @brief Applies one zoom (anchored at (anchorX, anchorY); negative anchor = viewport center)
        void applyZoom(float newScale, float anchorX, float anchorY);

        /// @brief Zooms by a factor (common body of `ZoomIn` / `ZoomOut`; viewport center as anchor)
        /// @note Before the original image size is available (before the first frame) only `wantZoom_` is recorded, applied on the first frame.
        void zoomBy(float factor);

        /// @brief File name of image index (takes the last segment cross-platform)
        std::string fileNameOf(const std::string& path) const;

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        /// @brief Notifies "image changed" (resets the view + emits the callback)
        void notifyIndexChanged(int index);

        // ---- Data ----
        std::vector<std::string> sources_;
        int         index_ = -1;          /// < current index (-1 = no image)
        std::string title_;               /// < title override (empty = use the file name)
        bool        loop_ = false;        /// < whether paging wraps
        int         pendingIndex_ = 0;    /// < index to land on after SetSources

        // ---- Display state ----
        bool  open_ = false;
        long long openAtMs_ = 0;          /// < open time (fade-in start; steady_clock milliseconds)

        // ---- View (factor / offset) ----
        //  scale_ = screen pixels / image pixels (absolute value, convenient for cursor-anchored conversion)
        //  zoom  = scale_ / fitScale()          (external semantics: 1 = fit window)
        mutable float scale_ = 0.0f;      /// < absolute zoom (0 = not yet computed, filled by fit on the next frame)
        mutable float fitScaleCache_ = 0.0f;  /// < last computed fit factor (used to convert zoom externally)
        float panX_ = 0.0f;               /// < offset of the image center relative to the viewport center (screen pixels)
        float panY_ = 0.0f;
        bool  fitted_ = true;             /// < true = follow fit window (becomes false once the user has zoomed)
        /// @brief "switch to 1:1 once the original image size is available" (set when `ActualSize()` is called before the first frame)
        /// @note The size is only obtainable during paint (`FrameCollector::ImageSize`), so
        ///       a call that requests 1:1 before any paint must record the intent and apply it on the first frame --
        ///       otherwise it would be silently dropped and the user would see "clicked, nothing happened".
        mutable bool wantActual_ = false;

        /// @brief "apply this factor once the original image size is available" (set when `SetZoom` / `ZoomIn` / `ZoomOut`
        ///        are called before the first frame; 0 = no pending intent)
        /// @details Same kind as `wantActual_`: when the size is not yet available `fitScale()` returns 0,
        ///          so the "factor relative to fit window" cannot be converted to an absolute zoom -- so the intent is recorded
        ///          and applied on the first paint. `Show(...)` immediately followed by `SetZoom(2.0f)` is
        ///          the most common usage and must work.
        mutable float wantZoom_ = 0.0f;
        float zoomStep_ = 1.15f;
        float minZoom_ = 0.1f;
        float maxZoom_ = 20.0f;

        /// @brief Original image pixel size (mutable: `paintContent` is const but must cache it for hit/wheel use)
        mutable float imgPxW_ = 0.0f;
        mutable float imgPxH_ = 0.0f;

        // ---- Interaction transient ----
        bool  dragging_ = false;
        float dragLastX_ = 0.0f, dragLastY_ = 0.0f;
        bool  hoverClose_ = false;
        bool  hoverPrev_ = false;
        bool  hoverNext_ = false;
        int   pressedPart_ = 0;           /// < 0 = background, 1 = close, 2 = previous, 3 = next
        long long lastClickMs_ = 0;       /// < double-click detection
        float     lastClickX_ = 0.0f, lastClickY_ = 0.0f;
        bool      doubleClickTogglesFit_ = true;
        bool      clickBgCloses_ = false;

        // ---- Appearance ----
        bool    showTopBar_ = true;
        bool    showBottomBar_ = true;
        bool    showCloseButton_ = true;
        bool    showNavArrows_ = true;
        /// @brief Mask color (= preview background): alpha 0 = fully transparent (image only) /
        ///        0<alpha<1 = translucent / 1 = solid. Translucent by default.
        Color4f scrim_{ 0.0f, 0.0f, 0.0f, 0.55f };

        /// @brief Native overlay carrier (whole-window covering; non-null only on platforms with desktop Popup capability)
        /// @note `mutable`: syncing happens in `Paint` (const)
        mutable std::unique_ptr<OverlayWindow> overlay_;
        /// @brief Last whole-window geometry pushed to the carrier (debounce)
        mutable float overlayW_ = 0.0f, overlayH_ = 0.0f;
        Color4f glyphColor_{ 1.0f, 1.0f, 1.0f, 0.86f };       /// < widget glyph color
        Color4f glyphHoverBg_{ 1.0f, 1.0f, 1.0f, 0.16f };     /// < widget hover background
        TextStyle titleStyle_;            /// < title (top bar left)
        TextStyle infoStyle_;             /// < caption text (bottom bar / index)
        float   topBarHeight_ = 44.0f;    /// < top bar height (theme injection)
        float   bottomBarHeight_ = 30.0f; /// < bottom bar height (theme injection)
        float   viewPadding_ = 16.0f;     /// < padding between the image and the viewport edges

        // ---- Callbacks ----
        SelfAction onClosed_;             /// < close notification
        IndexAction onIndexChanged_;      /// < image-change notification
    };

} // namespace Kit
