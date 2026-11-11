#pragma once
/**
 * @file ZeEmbeddedRes.h
 * @brief Embeds images "into the executable" -- a cross-platform embedded resource registry
 *
 * @details
 * Windows has `.rc` + `RT_RCDATA`: resources are naturally packed into the exe, and `FindResource` fetches them in one line.
 * Other platforms (Linux / macOS / Android ...) have no counterpart -- native executables have no such thing as a "named resource section". So the library provides this very thin registry that takes over the "resource ID -> memory bytes" job itself:
 *
 * @code
 * // (1) Build time: convert png to a C array (or use objcopy -I binary to make a symbol in a .o)
 * const unsigned char kLogoPng[] = { 0x89, 0x50, 0x4E, 0x47, ... };
 *
 * // (2) Startup (static construction or in main, either works): register once
 * Kit::RegisterEmbeddedResource(102, kLogoPng, sizeof(kLogoPng));
 *
 * // (3) Use it exactly as on Windows -- the interface already exists, no changes needed
 * win.SetWindowIconFromResource(102);
 * icon.SetImageFromResource(102);
 * @endcode
 *
 * ## Relationship with Windows
 *
 * This layer does not replace `.rc`; the two are "two supply lines for the same ID":
 *   - Windows: `ImageStore::LoadFromResource` / `X11Window::SetIconFromResource`
 *     go through `FindResourceW(...)` to get bytes -- this table is never consulted;
 *   - Other platforms: consult this table.
 * So the same application code needs no `#ifdef`; where resources come from is decided by the platform layer.
 * (The implementation also keeps a table-lookup branch for Windows: in case some platform disables rc and keeps only png,
 *  it still works.)
 *
 * ## Lifetime
 *
 * The table stores raw pointers; it does not copy and does not take ownership -- the data must live until process exit.
 * Embedded arrays satisfy this naturally (static storage duration); manage dynamically allocated memory yourself.
 * When the same ID is registered again, the later registration overrides the earlier one (convenient for hot reload / skin switching).
 *
 * @note Thread safety: both registration and lookup take a lock. Normal usage registers once at startup and then only reads;
 *       there is no observable overhead on this path.
 */

#include <cstddef>

namespace Kit {

    /// @brief A read-only view of resource bytes (does not own them)
    struct EmbeddedResource {
        const void* data = nullptr;   /// < First byte; nullptr = not found
        std::size_t size = 0;         /// < Byte count

        explicit operator bool() const noexcept { return data != nullptr && size > 0; }
    };

    /**
     * @brief Registers an embedded resource (a later registration with the same ID overrides an earlier one)
     * @param id    Resource ID -- shares the same numeric space as the number in Windows `.rc`
     *              (usually a macro in `resource.h`, such as `IDR_ZEKIT_LOGO`)
     * @param data  First byte of the resource, must live until process exit (pass a static array)
     * @param size  Byte count
     *
     * @note Silently ignored when `id <= 0` or `data == nullptr` or `size == 0` --
     *       a bad resource registration should not crash the program at startup; if it cannot be fetched, treat it as unregistered.
     */
    void RegisterEmbeddedResource(int id, const void* data, std::size_t size);

    /**
     * @brief Looks up a resource by ID
     * @return `{data, size}` if found, otherwise `{nullptr, 0}` (`if (r)` tests for empty)
     *
     * @note The returned view is valid only until the next registration (registration rebuilds the table entry).
     *       The pointer itself points to the caller-provided static data and does not become invalid.
     */
    EmbeddedResource FindEmbeddedResource(int id);

} // namespace Kit
