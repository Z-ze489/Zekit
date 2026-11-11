/**
 * @file ZeFileDropZone.h
 * @brief File drop zone: an area that accepts files dragged in from the file explorer
 *
 * @details
 * `FileDropZone : TextWidget` -- box and text styles, padding and alignment are all inherited;
 * this class only adds the three things a "drop zone" actually needs:
 *
 *  1. Becomes a drop target: calls `SetAcceptFileDrop(true)` automatically on construction, and
 *     also opens the drop channel of the owning window (only then does the platform layer
 *     install `IDropTarget`). Application code never has to touch the window.
 *  2. Visual feedback while dragging: `OnFileDragEnter/Over/Leave` maintain `dragActive_`,
 *     and painting switches to the theme's `activeBox` / `activeText` -- the border turns the
 *     primary color and the background gets a light primary tint.
 *     This is the only capability `IDropTarget` buys over `WM_DROPFILES`; do not waste it.
 *  3. Extension filtering: after `SetExtensions({"csv","txt"})`, drags that do not contain
 *     these extensions are not accepted (no highlight, no callback on release), so the user
 *     is not confused when dragging the wrong thing.
 *
 * @par Typical usage
 * @code
 * Kit::FileDropZone zone("Drop CSV here\nor click the button on the right to pick a file");
 * zone.SetGeometry(24, 24, 320, 160);
 * zone.SetExtensions({ "csv", "tsv" });          // optional: table files only
 * zone.SetOnFilesDropped([&](const std::vector<std::string>& files) {
 *     for (const auto& f : files) table.LoadFrom(f);
 * });
 * win.AddWidget(zone);
 * @endcode
 *
 * @note This and `Kit::FileDialog` are two complementary paths, not substitutes: drag and drop
 *       is fast and suits the "file explorer is already open" case; the dialog can browse and
 *       suits the "no idea where the file is" case. A real product UI usually offers both.
 * @note When the platform does not support drag and drop (`Capability::FileDrop` is false) this
 *       widget still displays, it just never receives files -- so it should not be the only
 *       entry point.
 *
 * @see ZeFileDialog.h native system picker dialog (the right path for keyboard / platforms
 *      without drag and drop)
 * @see the OnFileDragEnter family in ZeWidget.h (virtual-function-level interface)
 */

#pragma once
#include "ZeLabel.h"

#include <functional>
#include <string>
#include <vector>

namespace Kit {

    class FileDropZone : public Label {
    public:
        /// @brief Constructs the drop zone
        /// @param hint Main hint text (may contain '\n'; use `\n` to split lines)
        /// @note The default text is "Drop files here". It becomes a drop target on construction --
        ///       there is no need to call `SetAcceptFileDrop` again.
        explicit FileDropZone(std::string hint = std::string());

        // -------------------------------------------------------------------
        //  Text
        // -------------------------------------------------------------------

        /// @brief Main hint (large text)
        void SetHint(const std::string& hint);
        /// @brief Sub hint (small text; overwritten by "Release to drop N files" while dragging)
        /// @param sub Empty string = no sub hint (main hint is vertically centered)
        void SetSubHint(const std::string& sub);

        // -------------------------------------------------------------------
        //  Behavior
        // -------------------------------------------------------------------

        /// @brief Callback after files are dropped (runs on the UI thread)
        /// @param cb Argument is the absolute paths of the dropped files (UTF-8)
        void SetOnFilesDropped(std::function<void(const std::vector<std::string>&)> cb);

        /// @brief Whether multiple files may be dropped at once (default true)
        /// @note When disabled, only the first file is taken and passed to the callback -- the whole
        ///       drop is not rejected: if the user drags three files in, giving them at least one beats
        ///       "nothing happened".
        void SetMultiple(bool on);
        bool multiple() const { return multiple_; }

        /// @brief Accepts only these extensions (case-insensitive; `".csv"` / `"csv"` / `"*"` all work)
        /// @param exts Empty array = accept any file (default)
        /// @note Directories do not participate in extension filtering -- that is controlled separately
        ///       by `SetAcceptDirectories`.
        void SetExtensions(std::vector<std::string> exts);
        const std::vector<std::string>& extensions() const { return extensions_; }

        /// @brief Whether directories are accepted (default false)
        /// @note When a directory is dropped, `files` contains the directory path; the application
        ///       decides whether to recurse.
        void SetAcceptDirectories(bool on);
        bool acceptsDirectories() const { return acceptDirs_; }

        // -------------------------------------------------------------------
        //  State query / appearance
        // -------------------------------------------------------------------

        /// @brief Whether files are currently dragged over this widget (used by painting, also for
        ///        external coordination)
        bool isDragActive() const { return dragActive_; }
        /// @brief Number of files currently dragged over (0 when not dragging)
        int dragFileCount() const { return dragCount_; }

        /// @brief Text color in the drag-active state (overrides the theme)
        void SetActiveTextColor(const Color4f& c);
        /// @brief Background color in the drag-active state (overrides the theme's
        ///        activeBox.background)
        void SetActiveBackColor(const Color4f& c);
        /// @brief Border color in the drag-active state (overrides the theme's activeBox.border)
        void SetActiveBorderColor(const Color4f& c);

        // -------------------------------------------------------------------
        //  Drop callbacks (overrides of base-class virtual functions)
        // -------------------------------------------------------------------
        bool OnFileDragEnter(float x, float y, const std::vector<std::string>& files) override;
        bool OnFileDragOver(float x, float y) override;
        void OnFileDragLeave() override;
        bool OnFileDrop(float x, float y, const std::vector<std::string>& files) override;

    protected:
        /// @brief Default box style comes from the theme's fileDropZone section (the normal-state
        ///        entry)
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style comes from the theme's fileDropZone section
        const TextStyle& themeText(const Theme& t) const override;
        /// @brief Re-injects the theme: besides the base-class part, also takes the drag-active box
        ///        and sub-hint styles
        void ApplyTheme(const Theme& t) override;

        /// @brief Paints the main hint / sub hint with two separate styles (the sub hint is smaller
        ///        and fainter)
        /// @note Why not just use `Label::paintContent`: that widget has only one `TextStyle`, so the
        ///       `fileDropZone.subText` configured in the theme (smaller font size, `muted` color, 0.8
        ///       alpha) has nowhere to take effect -- `subStyle_` used to be assigned but read nowhere
        ///       in the library, and the two lines looked identical. Here each line takes its own style,
        ///       while vertically the "main segment + spacing + sub segment" is still centered as one
        ///       block.
        void paintContent(FrameCollector& fc) const override;
        /// @brief Natural height must include the sub-hint segment (otherwise the block clips itself)
        float hintHeight() const override;
        /// @brief Natural width is the widest line of the two segments
        float hintWidth() const override;

    private:
        /// @brief Whether any of these files is accepted by this widget
        bool AcceptsThese(const std::vector<std::string>& files) const;
        /// @brief Picks out the accepted files by extension (keeps only the first when
        ///        `SetMultiple(false)`)
        std::vector<std::string> Filter(const std::vector<std::string>& files) const;
        /// @brief Toggles the drag-active state (idempotent; requests a repaint only on an actual
        ///        change)
        void SetDragActive(bool on, int count);
        /// @brief Assembles the current state (dragging or not / main and sub hints) into the Label's
        ///        text_
        /// @note Why go around this way: the Label paint path (multi-line splitting + centering +
        ///       ellipsis) is the one repeatedly verified in the library; writing `MeasureText +
        ///       paintTextAt` again has hit the "every other character dropped" bug (see the note in
        ///       ZeTheme.h). So here the widget is a Label and only decides which lines to show.
        void RefreshText();

        std::string hint_;        /// < Main hint
        std::string subHint_;     /// < Sub hint (empty = not painted)

        std::function<void(const std::vector<std::string>&)> onDropped_;

        bool multiple_ = true;                 /// < Allow multiple
        bool acceptDirs_ = false;              /// < Accept directories
        std::vector<std::string> extensions_;   /// < Extension whitelist (empty = accept all)

        bool dragActive_ = false;   /// < Files are dragged over
        int  dragCount_ = 0;        /// < Number of files dragged over

        // Drag-active appearance (from the theme, overridable by SetActive*)
        BoxStyle  activeBox_;                 /// < Drag-active box (border / background)
        Color4f   activeText_{ 0.0f, 0.0f, 0.0f, 1.0f };  /// < Drag-active text color
        TextStyle subStyle_;                  /// < Sub-hint text style (smaller and fainter)
        /// @brief Snapshot of the normal-state box (`box_` is swapped for activeBox_ while dragging,
        ///        restored on release / leave)
        /// @note Why not store `styleTheme_` and re-inject: that would also wipe the user's SetBox
        ///       changes. The snapshot is "the one from before entering the drag-active state"; only a
        ///       byte-for-byte restore loses nothing.
        BoxStyle  savedBox_;

        // Override flags for user-modified values (true = the theme no longer overrides it)
        bool overrideActiveText_ = false;
        bool overrideActiveBack_ = false;
        bool overrideActiveBorder_ = false;
        Color4f activeTextOverride_{ 0.0f, 0.0f, 0.0f, 1.0f };   /// < Value recorded by SetActiveTextColor
        Color4f activeBackOverride_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < Value recorded by SetActiveBackColor
        Color4f activeBorderOverride_{ 0.0f, 0.0f, 0.0f, 0.0f }; /// < Value recorded by SetActiveBorderColor
    };

} // namespace Kit
