/**
 * @file ZeCursorKind.h
 * @brief Mouse cursor style (leaf enum header -- the widget layer and the platform layer share the same set of values)
 *
 * @details
 * Split out separately so that `Plat::IWindow::SetMouseCursor()` can also reference it, without
 * pulling the whole `ZeBoxStyle.h` (along with `image_types.h` etc.) into the platform layer.
 * This is consistent with the approach of `Kit::Capability` / `Kit::Key`: a leaf enum in a public header,
 * shared by both layers as one set of values, with no secondary mapping (a mapping table is the source of drift).
 *
 * @note The value order is unrelated to Win32 `IDC_*` -- the platform layer handles the mapping itself,
 *       and switching platforms (X11 cursor font / macOS NSCursor) only requires swapping that mapping table.
 *
 * @see ZeBoxStyle.h   `BoxStyle::cursor` (legacy field, see the note there)
 * @see ZeWidget.h     `Widget::SetCursor` (widget-level declaration)
 */

#pragma once

namespace Kit {

    /// @brief Mouse cursor style
    enum class Cursor {
        Arrow,        /// < Default arrow
        Pointer,      /// < Hand (clickable / selector)
        Text,         /// < I-beam (text selectable / editable)
        NotAllowed,   /// < Not allowed
        Wait,         /// < Busy
        Grab,         /// < Grabbable
        Grabbing,     /// < Grabbing
        SizeWE,       /// < Horizontal resize
        SizeNS        /// < Vertical resize
    };

} // namespace Kit
