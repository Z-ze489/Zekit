// image_types.h - public image/texture types: handle, resource, draw parameters.
// Uses uint32_t instead of GLuint so users never include GL/DX headers.

#pragma once
#include <cstdint>

namespace Kit {

    // Texture handle; 0 = no image. Created and released by the library.
    using ImageHandle = uint32_t;

    // A loaded image: handle + original pixel size (avoids API queries at draw time).
    struct BackgroundImage {
        ImageHandle handle = 0;  // 0 = no image
        int width = 0;           // original pixel width
        int height = 0;          // original pixel height
    };

    // How the image fits the target rect, like CSS background-size.
    // The image is always clipped to the rect, Origin included.
    enum class BgImageDrawMode {
        Stretch,    // fill the rect, ignores aspect ratio (may distort)
        Fit,        // keep aspect, contain (may leave blank space)
        Fill,       // keep aspect, cover (crops the overflow)
        Tiled,      // repeat at original size
        Origin      // no scaling, placed by hAlign/vAlign
    };

    // Used by Origin only; ignored in the other modes.
    enum class BgImageHAlign {
        Left,
        Center,
        Right
    };

    enum class BgImageVAlign {
        Top,
        Middle,
        Bottom
    };
}
