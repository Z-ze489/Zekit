/**
 * @file image_types.hpp
 * @brief Common types for images and textures
 *
 * @details
 * This header defines all public types related to image resources in ZeWin, shared by widgets (button background images,
 * icons, image viewers, etc.) and layout containers:
 *  - @ref ImageHandle: texture handle alias that hides the underlying graphics API type;
 *  - @ref BackgroundImage: image resource description of texture handle plus original size;
 *  - @ref BgImageDrawMode / @ref BgImageHAlign / @ref BgImageVAlign:
 *    background image draw parameters (shared by buttons, layout containers, cards, etc.).
 *
 * The library decodes image resources and uploads textures internally; users usually only provide the file path,
 * and the widget (such as Button::SetBackgroundImage) loads the image and fills in the handle and size.
 *
 * @note Public headers use uint32_t instead of GL's GLuint, to avoid leaking graphics API headers
 *       to third-party developers; users need not include any OpenGL / DirectX headers.
 * @note Handle 0 always means "no image" and can be used as a validity check.
 *
 * @par Basic usage
 * @code
 * #include "image_types.hpp"
 *
 * Win::BackgroundImage bg;          // handle = 0, no image
 * btn.SetBackgroundDrawMode(Win::BgImageDrawMode::Fit);
 * btn.SetBackgroundAlign(Win::BgImageHAlign::Center,
 *                        Win::BgImageVAlign::Middle);
 * @endcode
 *
 * @see color4f.hpp color types
 * @see FontDesc.h font description types
 */

#pragma once
#include <cstdint>

namespace Kit {
    /**
     * @brief Texture handle type (integer identifier of the underlying graphics resource)
     *
     * @details
     * Actually the texture object ID of the graphics API. Public headers use uint32_t instead of GLuint,
     * to avoid leaking graphics API headers to third-party developers.
     *
     * @note A value of 0 means "no texture / no image" and can be used for validity checks.
     * @note The handle is created and released by the library; users should only store and pass it, not modify it.
     */
    using ImageHandle = uint32_t;

    /**
     * @brief A single image resource (with original size, avoiding GL lookups during rendering)
     *
     * @details
     * Besides the texture handle, the pixel width and height of the original image are cached, so the render stage
     * can perform proportional scaling, centering, tiling, etc. without querying the graphics API.
     *
     * Usually filled by the library when loading an image file (such as Button::SetBackgroundImage);
     * users may also assemble it manually when a texture already exists.
     *
     * @par Example
     * @code
     * Win::BackgroundImage bg;
     * if (bg.handle != 0) {
     *     float aspect = float(bg.width) / float(bg.height);
     * }
     * @endcode
     */
    struct BackgroundImage {
        ImageHandle handle = 0;  /// < Texture ID, 0 means no image
        int width = 0;           /// < Original image pixel width
        int height = 0;          /// < Original image pixel height
    };

    // ===================================================================
    //  Background image draw parameters (shared by buttons / layout containers, kept here to avoid duplicate definitions)
    // ===================================================================

    /**
     * @brief Background image draw mode
     *
     * @details
     * Determines how the background image fits the target rectangle, similar to CSS background-size:
     *  - Stretch: ignores aspect ratio and fills the rectangle (simplest, may distort);
     *  - Fit / Fill: keeps aspect ratio, corresponding to contain / cover;
     *  - Tiled: repeats at original size;
     *  - Origin: no scaling, positioned with BgImageHAlign / BgImageVAlign.
     *
     * @note Only Origin mode uses the alignment enums; alignment settings are ignored in other modes.
     * @see BgImageHAlign, BgImageVAlign
     */
    enum class BgImageDrawMode {
        Stretch,    /// < Stretches to fill the rectangle (distorts, most common)
        Fit,        /// < Scales proportionally to fit inside the rectangle, may leave blank space
        Fill,       /// < Scales proportionally to fill the rectangle, cropping the overflow
        Tiled,      /// < Tiled repeated drawing
        Origin      /// < Uses the original image size, positioned by hAlign/vAlign, no scaling
    };

    /**
     * @brief Horizontal alignment of the background image (only effective in Origin mode)
     * @see BgImageDrawMode::Origin, BgImageVAlign
     */
    enum class BgImageHAlign {
        Left,   /// < Original image left-aligned
        Center, /// < Original image horizontally centered
        Right   /// < Original image right-aligned
    };

    /**
     * @brief Vertical alignment of the background image (only effective in Origin mode)
     * @see BgImageDrawMode::Origin, BgImageHAlign
     */
    enum class BgImageVAlign {
        Top,    /// < Original image top-aligned
        Middle, /// < Original image vertically centered
        Bottom  /// < Original image bottom-aligned
    };
}