# Third-party notices

Zekit is MIT licensed. It bundles the following third-party components, each
keeping its own license. If you redistribute a binary built from this repository,
ship these notices with it.

| Component | Version | License | Where |
|---|---|---|---|
| FreeType | 2.14.1 | FreeType License (FTL) | `Zekit/third_party/freetype-2.14.1/` |
| GLM | 0.9.9.8 | MIT | `inc/glm/` |
| SOIL2 (incl. stb_image) | — | MIT | `inc/SOIL2/` |
| glad | 2.x (generated) | MIT | `inc/glad/` |
| nlohmann/json | 3.x (single header) | MIT | `inc/nlohmann/json.hpp` |
| Vulkan-Headers | 1.3.x | Apache-2.0 | `inc/Include/vulkan/` |

---

## FreeType License

FreeType is used for glyph rasterization and is compiled directly into `Zekit.lib`.
The full text is in `Zekit/third_party/freetype-2.14.1/LICENSE.TXT`. In short:

> Portions of this software are copyright © The FreeType Project
> (https://www.freetype.org). All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification,
> are permitted provided that the copyright notice and this permission notice
> appear in all copies, and that you do not use the name of the FreeType Project
> to endorse derived products without written permission.
>
> Alternatively, FreeType may be used under the terms of the GNU General Public
> License version 2. **Zekit uses the FTL option.**

Any redistributed binary must include the FreeType copyright notice above.

## MIT-licensed components

The following are distributed under the MIT License. The full text of the license
applies to each of them:

- **GLM** — Copyright (c) 2005-2024 G-Truc Creation. Full text: `inc/glm/copying.txt`.
- **SOIL2** — Copyright (c) Jonathan Dummer, SpartanJ. Simple OpenGL Image Library 2.
- **stb_image** — public domain / MIT (dual), bundled inside SOIL2.
- **glad** — Copyright (c) 2013-2024 David Herberth. Generated loader, MIT.
- **nlohmann/json** — Copyright (c) 2013-2024 Niels Lohmann.

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Vulkan-Headers

Only the headers are bundled (`inc/Include/vulkan/`), to compile against Vulkan at
build time. Vulkan itself is loaded at run time (`dlopen` / `LoadLibrary`) — Zekit
does not link against `vulkan-1`.

```
Copyright 2015-2024 The Khronos Group Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use these files except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.
```

Vulkan-Headers are dual licensed Apache-2.0 / MIT; Zekit relies on the
Apache-2.0 option.

## System libraries

On Windows the library links `user32`, `gdi32`, `advapi32`, `shlwapi`, `winmm`,
`imm32`, `gdiplus`, `ole32`, `shell32`, `d3d12`, `dxgi`, `d3dcompiler`, `uuid` and
`opengl32`. On Linux it links `X11`, `GL`, `EGL`, `dl` and `pthread`. Those are part
of the operating system and carry the platform's own terms; nothing is redistributed
with Zekit.
