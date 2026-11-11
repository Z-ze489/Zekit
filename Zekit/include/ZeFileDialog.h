/**
 * @file ZeFileDialog.h
 * @brief File dialogs: system-native open / save / pick folder
 *
 * @details
 * Two things live in this one header because they are one unit:
 *
 *  1. `FileDialogDesc` -- a **pure data** request description. It is used by both
 *     `Kit::FileDialog` (application side) and `Plat::IWindow::ShowFileDialog` (platform side).
 *     It lives in a public header and both sides share one definition for exactly the same reason as
 *     `ZeCapability.h`: two definitions inevitably drift apart, and they drift **silently** (the
 *     platform reads one field less, and nothing on screen reports an error).
 *  2. `FileDialog` -- a value-semantics chainable wrapper. Applications only deal with it.
 *
 * @par Why not a library-drawn file browser
 * Most of a file browser's value is system integration: quick access, network locations, search,
 * recent files, an IME-friendly path box, context menus, drag and drop. Redrawing one only makes it
 * worse, and "whether to switch to another browsing style" is a product decision that a widget
 * library should not force. So this only wraps the system-native implementation; if the platform
 * has none, it honestly returns false (`Capability::NativeFileDialog` is false).
 *
 * @par Usage
 * @code
 * Kit::FileDialog dlg;
 * dlg.SetTitle("Open market data")
 *    .AddFilter("CSV files", "*.csv")
 *    .AddFilter("All tables", "*.csv;*.tsv;*.xlsx")
 *    .SetInitialDirectory("D:/data")
 *    .AllowMultiple(true);
 *
 * if (dlg.ShowOpen(win)) {
 *     for (const std::string& path : dlg.files()) load(path);
 * }
 * @endcode
 *
 * @note `Show*` is **synchronous** (modal): when it returns, the user has already made a choice.
 *       Blocking the UI thread is exactly what a modal dialog should do -- the host is disabled by
 *       the system during this time and cannot be clicked.
 * @note Paths are always **UTF-8**, consistent with other strings in the library.
 *
 * @see ZeFileDropZone.h drag and drop (the other path for "getting files in")
 */

#pragma once

#include <string>
#include <vector>

namespace Kit {

    class Window;

    // ===================================================================
    //  Request description (pure data, shared by Kit and Plat)
    // ===================================================================

    /**
     * @brief **Request description** for a system-native file dialog
     *
     * @details
     * Expresses only "what the user wants to say", with no platform types -- each platform
     * translates it into `IFileOpenDialog` / portal / whatever itself.
     *
     * Filters are deliberately **two parallel arrays** (`filterLabels` / `filterPatterns` paired by
     * index) rather than `std::vector<std::pair<...>>`: this is a shape every platform can express
     * consistently, and it is more direct for callers to write ("label + wildcard").
     */
    struct FileDialogDesc {
        /// @brief Dialog kind (determines title bar semantics and button text)
        enum class Mode : int {
            Open = 0,    /// < Open file (multi-select allowed)
            Save,        /// < Save file (overwrite confirmation)
            PickFolder,  /// < Pick folder (`allowMultiple` has no effect on most platforms)
        };

        Mode        mode = Mode::Open;
        std::string title;          /// < Dialog title; empty = platform default ("Open" / "Save")
        std::string initialDir;     /// < Initial directory (UTF-8 absolute path); empty = system last location
        std::string defaultName;    /// < Default file name for save mode (including extension)
        std::string defaultExt;     /// < Default extension for save mode (without dot, e.g. "txt")

        std::vector<std::string> filterLabels;    /// < Filter display name ("Images" / "CSV files")
        std::vector<std::string> filterPatterns;  /// < Wildcards paired with labels ("*.png;*.jpg")
        int  filterIndex = 0;       /// < Index of the filter selected by default (0-based)
        bool allowMultiple = false; /// < Allow selecting multiple (Open only)
        bool overwritePrompt = true;/// < Whether to show an overwrite confirmation when the file already exists on Save
    };

    // ===================================================================
    //  Chainable wrapper
    // ===================================================================

    /**
     * @brief File dialog (value semantics, chainable configuration)
     *
     * @details
     * One `FileDialog` object = one "request". After configuring it, call `ShowOpen` / `ShowSave` /
     * `ShowPickFolder` to pop it up; the result stays in the object (`files()` / `file()` / `ok()`).
     *
     * The same object **can be reused** (each `Show*` first clears the previous result), so
     * experiences like "remember the last directory" need only one member variable.
     *
     * @note This class **owns** no platform resources and does not need the window to be alive -- it
     *       only borrows `owner` as the modal parent window during `Show*`.
     */
    class FileDialog {
    public:
        FileDialog() = default;

        // ---- Configuration (all return *this, chainable) ----

        /// @brief Dialog title (empty = platform default)
        FileDialog& SetTitle(std::string title);
        /// @brief Initial directory (UTF-8 absolute path; the platform falls back to the default location if it does not exist)
        FileDialog& SetInitialDirectory(std::string dir);
        /// @brief Default file name for save mode (including extension)
        FileDialog& SetDefaultName(std::string name);
        /// @brief Default extension for save mode (without dot, e.g. "txt")
        FileDialog& SetDefaultExt(std::string ext);

        /// @brief Appends a filter
        /// @param label Display name ("Images")
        /// @param patterns Wildcards, semicolon-separated ("*.png;*.jpg")
        /// @note As soon as any filter is given, the platform implementation **automatically appends**
        ///       "All files (*.*)" -- the user always has a way out (otherwise files whose extension is not
        ///       in the whitelist cannot be selected at all).
        FileDialog& AddFilter(std::string label, std::string patterns);
        /// @brief Clears all filters (back to "no filtering")
        FileDialog& ClearFilters();
        /// @brief Index of the filter selected by default (0-based)
        FileDialog& SetFilterIndex(int index);

        /// @brief Allows selecting multiple files (`ShowOpen` only, default false)
        FileDialog& AllowMultiple(bool on = true);
        /// @brief Whether to show "file already exists, overwrite?" on save (default true)
        FileDialog& SetOverwritePrompt(bool on);

        // ---- Execution (synchronous / modal) ----

        /// @brief Pops up the "Open" dialog
        /// @param owner Modal parent window (must already be created; a child window is fine, the top-level is taken internally)
        /// @return true = the user confirmed (result in `files()`); false = cancelled / not supported by the platform
        bool ShowOpen(Window& owner);
        /// @brief Pops up the "Save" dialog
        /// @return true = the user confirmed (result in `file()`)
        bool ShowSave(Window& owner);
        /// @brief Pops up the "Pick folder" dialog
        /// @return true = the user confirmed (result in `file()`)
        bool ShowPickFolder(Window& owner);

        // ---- Results ----

        /// @brief Whether the last `Show*` succeeded (equivalent to `!files().empty()`)
        bool ok() const { return ok_; }
        /// @brief All result paths (in user selection order for multi-select; empty if not confirmed)
        const std::vector<std::string>& files() const { return files_; }
        /// @brief First result path (empty string if not confirmed) -- convenience for single-select cases
        const std::string& file() const { return first_; }
        /// @brief Clears the previous result (no need to call manually when reusing an object, `Show*` clears it)
        void Clear();

        // ---- Capability query ----

        /// @brief Whether the platform of the given window supports **system-native** file dialogs
        /// @note When unsupported, `Show*` always returns false and prints one line to stderr
        ///       ("clicked and nothing happened" is the hardest failure to diagnose, so it is better to be
        ///       noisy). The real switch on the platform side is `Capability::NativeFileDialog`.
        static bool IsSupported(const Window& owner);

    private:
        /// @brief Common implementation of `Show*`
        bool show(Window& owner, FileDialogDesc::Mode mode);

        // Configuration
        std::string title_;
        std::string initialDir_;
        std::string defaultName_;
        std::string defaultExt_;
        std::vector<std::string> filterLabels_;
        std::vector<std::string> filterPatterns_;
        int  filterIndex_ = 0;
        bool allowMultiple_ = false;
        bool overwritePrompt_ = true;

        // Results
        bool ok_ = false;
        std::vector<std::string> files_;
        std::string first_;
    };

} // namespace Kit
