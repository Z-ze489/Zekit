#pragma once
/**
 * @file ZeSoftSurface.h
 * @brief Software rasterization presentation surface -- puts a block of CPU pixels into a native window
 *
 * @details
 * Rasterization (`ZeSoftRaster.h`) is platform independent: it only turns vertices into a block of `B,G,R,A`
 * bytes. How those bytes reach the screen differs per platform:
 *
 * | Platform | Mechanism | In one line |
 * |---|---|---|
 * | X11 | `XPutImage` | The protocol itself is "send an image to the server", mature by nature |
 * | Win32 | `CreateDIBSection` + `BitBlt` | The classic GDI path, zero third-party dependencies |
 * | Others (Android / HarmonyOS / Apple) | None yet | `CreateSoftSurface()` returns nullptr |
 *
 * This file is the only seam between those two things: `Rhi::SoftBackend` only knows `ISoftSurface`,
 * so the assembly code of the software backend (`ZeRenderBackendSoft.cpp`) is written once,
 * and all platform differences live in the compile-time branches of `ZeSoftSurface.cpp`.
 *
 * @note Same division of labor as `ZeRenderBackendLinux.cpp` / `ZeRenderBackendWin32.cpp`:
 *       platform headers (`<Xlib.h>` / `<Windows.h>`) may only appear in .cpp files,
 *       this header stays free of platform dependencies -- it is compiled into every platform build.
 * @note The pixel format is fixed to memory order `B,G,R,A` with premultiplied alpha: X11 24/32-bit
 *       TrueColor `ZPixmap` and Win32 32bpp `BI_RGB` DIB both use this byte order,
 *       so the presentation side can `memcpy` whole rows without reordering a single pixel.
 *
 * @see ZeSoftRaster.h           How the pixels are computed
 * @see ZeRenderBackendSoft.cpp  How the two are assembled into an `Rhi::IBackend`
 */

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Rhi {

    /**
     * @brief A pixel target that a native window can consume
     *
     * @details
     * Lifecycle: `Attach` (bind window) -> per-frame `Present` (may trigger an internal `Resize`) ->
     * `Detach` (release platform resources). `Present` / `Resize` / `Detach` must be idempotent,
     * because both call sites of `Rhi::IBackend` (`exec()` normal exit, `~Window()` fallback)
     * reach `Shutdown`.
     *
     * @note A failed `Attach` must clean up after itself (same rule as `IBackend::Init`).
     */
    class ISoftSurface {
    public:
        virtual ~ISoftSurface() = default;

        ISoftSurface(const ISoftSurface&) = delete;
        ISoftSurface& operator=(const ISoftSurface&) = delete;

        /// @brief Binds a native window and prepares a width x height pixel target
        /// @param nativeHandle The return value of `Plat::IWindow::NativeHandle()`
        ///        (on X11 it is restored to an XID via `Plat::X11WindowId`)
        virtual bool Attach(void* nativeHandle, int width, int height) = 0;

        /// @brief Sends one full frame of pixels to the window
        /// @param pixels First address (`B,G,R,A` premultiplied)
        /// @param pitch  Bytes per row (= 4 * width)
        /// @return false = this frame did not make it (degenerate size / platform resources gone)
        /// @note When the pixel size differs from the one given to `Attach`, the implementation rebuilds the target
        ///       itself -- the caller does not need to (and should not) ask again before every frame.
        virtual bool Present(const std::uint8_t* pixels,
                             std::size_t pitch, int width, int height) = 0;

        /// @brief Rebuilds the pixel target (resize)
        virtual bool Resize(int width, int height) = 0;

        /// @brief Whether this target really carries an alpha channel the system can composite
        /// @return true = alpha written into the buffer is read by the compositor (X11 32-bit ARGB window);
        ///         false = presentation is opaque (Win32 GDI / 24-bit window).
        ///
        /// @note This is a runtime fact, not a capability declaration -- same contract as
        ///       `IBackend::IsPerPixelTransparent`: reporting true without actual alpha makes the whole window disappear.
        virtual bool HasAlphaChannel() const { return false; }

        /// @brief Releases platform resources (idempotent)
        virtual void Detach() = 0;

        /// @brief Short name of this implementation (for logs, e.g. "X11/XPutImage")
        virtual const char* Name() const { return "none"; }

    protected:
        ISoftSurface() = default;
    };

    /**
     * @brief Creates a presentation surface for this platform
     * @return The platform implementation; returns nullptr when this platform has no implementation
     *         (the caller treats it as "software backend unavailable")
     *
     * @note Returning nullptr is not the end of the error path: `SoftBackend::Init` prints a line
     *       saying which path is unimplemented and which file to implement it in, far easier to trace than a silent failure.
     */
    std::unique_ptr<ISoftSurface> CreateSoftSurface();

} // namespace Rhi
