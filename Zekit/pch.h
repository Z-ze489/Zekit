// pch.h: This is a precompiled header file.
// The files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, if any of the files listed here are updated between builds, they will all be recompiled.
// Do not add files that are frequently updated here, as this will negate the performance benefit.

#ifndef PCH_H
#define PCH_H

// ---- Platform availability gate (MUST be first) ----
// Every .cpp in the library includes this header, so placing the gate here means when compiling for HarmonyOS / Apple
// any .cpp will immediately #error, and there will be no half-baked state where some files compile and others do not.
// See include/ZePlatformGate.h.
#include "include/ZePlatformGate.h"

// Add headers to precompile here
#include "framework.h"

// ---- Zekit link requirements ----
// Placing these in pch.h (rather than pch.cpp) is intentional: pch.h is included by every .cpp,
// so every .obj carries these /DEFAULTLIB: directives and the linker is guaranteed to see them.
// If written in pch.cpp they would have no effect -- that obj contains only #include and produces no symbol references,
// so the directives are dead letters and consumers get a string of LNK2001 (wgl* / PathFindFileNameW).
//
// Third-party libraries (freetype / soil2) are no longer linked here: they are merged wholesale into Zekit.lib itself by the build script
// (the lib.exe merge step in build_zekit.sh),
// so consumers only need to link the single file Zekit.lib, with no third-party library paths.
// WARNING: Do not add #pragma comment(lib, ...) with absolute paths here -- that gets baked into the obj,
//    and downstream machines without that path get LNK1104.
//
// WARNING: `#pragma comment` is an MSVC-specific directive. This file is included by every .cpp,
//    including ZeWindowX11.cpp on Linux -- on GCC/Clang these lines are unknown pragmas,
//    which at best produce a string of warnings and at worst are rejected as compile failures by -Werror. So the whole block is guarded by _MSC_VER:
//    non-MSVC compilers do not even see it (there, linking is handled by the build script's -lX11 / -lGL).

#if defined(_MSC_VER)
// Windows SDK libraries: file name only is enough, the SDK library directory is always on the linker search path
#pragma comment(lib, "advapi32.lib")   // Registry: RegGetValueW (reads system dark mode)
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d12.lib")   // D3D12 device / command queue / fence
#pragma comment(lib, "dxgi.lib")    // DXGI factory / swap chain (CreateDXGIFactory2 is in dxgi)
#pragma comment(lib, "d3dcompiler.lib") // Runtime HLSL compilation (D3DCompile)
#pragma comment(lib, "ole32.lib")   // OleInitialize / CoCreateInstance / drag and drop (OLE)
#pragma comment(lib, "shell32.lib") // DragAcceptFiles / DragQueryFileW / SHCreateItemFromParsingName
#pragma comment(lib, "uuid.lib")    // CLSID_FileOpenDialog / CLSID_FileSaveDialog / IID_IDropTarget
#endif // _MSC_VER

#endif //PCH_H
