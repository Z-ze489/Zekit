# =====================================================================
#  Zekit - unified CMake build
# =====================================================================
#  What comes out: ONE self-contained static library. FreeType, SOIL2 and glad
#  are compiled into it, so downstream links a single file and needs neither a
#  Vulkan SDK nor a FreeType install:
#      Windows : x64/<Config>/Zekit.lib      (path kept identical to the old VS project)
#      Linux   : build/<dir>/libZekit.a
#
#  How to build (Windows / MSVC):
#      cmake --preset windows-msvc
#      cmake --build --preset windows-msvc-release          # -> x64/Release/Zekit.lib
#
#  Facade projects (single-file exe, cannot ask users to install the VC++
#  runtime) use the static-CRT variant instead:
#      cmake --preset windows-msvc-mt
#      cmake --build --preset windows-msvc-mt-release       # -> x64/ReleaseMT/Zekit.lib
#      NOTE: object files from the two CRTs must never be linked together
#            (see "Runtime library variants" below).
#
#  How to build (Linux):
#      cmake --preset linux-release && cmake --build --preset linux-release
#
#  Why CMake and not shell scripts: the same source list used to be maintained
#  by hand in four places (vcxproj / build_zekit.sh / build_zekit_linux.sh /
#  the android and apple CMakeLists). Missing one did not fail the build; it
#  silently produced a library with fewer symbols, and downstream failed with
#  LNK2001. The list now lives in exactly one place, cmake/ZekitSources.cmake:
#  cross-platform files are globbed automatically, platform-only files are
#  registered in one table.
# =====================================================================
cmake_minimum_required(VERSION 3.22)

project(Zekit
    VERSION 1.0.0
    DESCRIPTION "C++20 desktop GUI toolkit (window, renderer and widget system included)"
    LANGUAGES C CXX)

# The Apple Metal backend is .mm (Objective-C++)
if(APPLE AND NOT ANDROID)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)     # -std=c++20, not gnu++20

# ---- Directory conventions ----
set(ZEKIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${ZEKIT_ROOT}/cmake/ZekitSources.cmake")

# ---- Platform detection (ANDROID must be tested before UNIX: it is UNIX) ----
if(WIN32)
    set(ZEKIT_PLATFORM WINDOWS)
elseif(ANDROID)
    set(ZEKIT_PLATFORM ANDROID)
elseif(APPLE)
    set(ZEKIT_PLATFORM APPLE)
elseif(UNIX)
    set(ZEKIT_PLATFORM LINUX)
else()
    message(FATAL_ERROR "Zekit supports Windows / Linux / Android / HarmonyOS / Apple; this platform is not adapted yet")
endif()

# =====================================================================
#  Runtime library (CRT) variants
# =====================================================================
#  Why two:
#    - /MD is the default (MultiThreadedDLL). It is what a normal consumer of
#      the library wants: small objects, shared CRT, no rebuild when the
#      runtime is updated.
#    - /MT (MultiThreaded) is for facade binaries - the manual reader and the
#      demos - which are shipped as a single exe and must start on a machine
#      that never installed the VC++ runtime.
#
#  WARNING: objects built against the two CRTs must NEVER be linked together.
#  Mixing them gives:
#      LNK2038 "RuntimeLibrary mismatch: MD_DynamicRelease vs MT_StaticRelease"
#      plus a pile of LNK2001 for __imp_lround / __imp_freopen /
#      __imp___stdio_common_vsscanf (the __imp_ prefix means the library wants
#      the DLL CRT while you are linking the static one).
#  So when this is ON the output goes to x64/<Config>MT/, a different name in a
#  different directory: grabbing the wrong library is then an immediate LNK1104
#  "cannot open file" instead of an LNK2038 halfway through the link.
#
#  Usage:
#      cmake --preset windows-msvc-mt          # -> x64/ReleaseMT/Zekit.lib
#      cmake --build --preset windows-msvc-mt-release
option(ZEKIT_STATIC_CRT
    "Build with the static CRT (/MT); output goes to x64/<Config>MT for facade binaries" OFF)

if(MSVC)
    if(ZEKIT_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    # How this variable is read: once, when a target is created. It must be set
    # before the first target() call below.
endif()

# =====================================================================
#  Third party: FreeType / SOIL2 / glad
# =====================================================================
#  All three are built as OBJECT libraries and their objects are spliced into
#  the zekit static library itself. That is how "downstream links one
#  Zekit.lib" is implemented (the old scripts merged .obj files with lib.exe to
#  get the same effect). OBJECT rather than STATIC because a static library
#  does not absorb its dependencies: the objects have to actually land in the
#  archive for the result to be self-contained.

zekit_freetype_sources(ZEKIT_FT_SOURCES)
add_library(zekit_freetype OBJECT ${ZEKIT_FT_SOURCES})
# FreeType's headers come straight from the vendored source tree. There used to
# be a second copy under inc/freetype; it was byte-identical apart from line
# endings, so having two of them only created the risk of editing one and
# wondering why nothing changed.
target_include_directories(zekit_freetype PRIVATE
    "${ZEKIT_FT}/include")
target_compile_definitions(zekit_freetype PRIVATE FT2_BUILD_LIBRARY)
if(MSVC)
    target_compile_definitions(zekit_freetype PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    target_compile_options(zekit_freetype PRIVATE /w)     # third-party code, not our warning bar
else()
    target_compile_options(zekit_freetype PRIVATE -w)
endif()

# SOIL2: image decoding. WARNING: this is an either/or with ZeSoilCompat.cpp -
# both define the same symbols. Linux builds only ZeSoilCompat (compiling
# SOIL2.c there would need GLX headers, while ImageStore only uses three pure
# CPU functions); every other platform compiles SOIL2.c.
if(NOT ZEKIT_PLATFORM STREQUAL "LINUX")
    add_library(zekit_soil2 OBJECT
        "${ZEKIT_INC}/SOIL2/SOIL2.c"
        "${ZEKIT_INC}/SOIL2/image_DXT.c"
        "${ZEKIT_INC}/SOIL2/image_helper.c"
        "${ZEKIT_INC}/SOIL2/image_array.c"
        "${ZEKIT_INC}/SOIL2/wfETC.c")
    target_include_directories(zekit_soil2 PRIVATE "${ZEKIT_INC}")
    if(MSVC)
        target_compile_definitions(zekit_soil2 PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
        target_compile_options(zekit_soil2 PRIVATE /w)
    else()
        target_compile_options(zekit_soil2 PRIVATE -w)
    endif()
endif()

# =====================================================================
#  Zekit itself: Plat window layer + Kit widget layer + Rhi render layer
# =====================================================================
zekit_collect_sources(${ZEKIT_PLATFORM} ZEKIT_SOURCES)

add_library(zekit STATIC ${ZEKIT_SOURCES})
add_library(zekit::zekit ALIAS zekit)

set_target_properties(zekit PROPERTIES
    OUTPUT_NAME "Zekit"                  # capitalisation matches the historical artifact name
    POSITION_INDEPENDENT_CODE ON)        # must be usable from shared libraries on Linux

target_sources(zekit PRIVATE $<TARGET_OBJECTS:zekit_freetype>)
if(TARGET zekit_soil2)
    target_sources(zekit PRIVATE $<TARGET_OBJECTS:zekit_soil2>)
endif()

# glad.c is C, not C++ (it relies on implicit void* conversions that are illegal
# in C++). CMake picks the compiler by extension, so it can go straight into the
# source list; the C++ side still resolves the symbols because glad.h wraps its
# declarations in extern "C".
# Android uses the system GLES3 headers (direct functions, no loader) and Apple
# goes through Metal - neither carries glad.
if(NOT ZEKIT_PLATFORM STREQUAL "ANDROID" AND NOT ZEKIT_PLATFORM STREQUAL "APPLE")
    target_sources(zekit PRIVATE "${ZEKIT_SRC}/lib/glad.c")
endif()

# ---- Include surface ----
# PUBLIC: downstream only needs -I Zekit/include to #include <Zekit.h>
# PRIVATE: the implementation needs (1) the internal header directory, which is
#          the base for "include/xxx.h" style includes, (2) the third-party
#          headers (SOIL2 / glad / glm / vulkan), and (3) FreeType's headers,
#          which the wrappers include as <ft2build.h>.
target_include_directories(zekit PUBLIC
    "${ZEKIT_SRC}/include")
target_include_directories(zekit PRIVATE
    "${ZEKIT_SRC}"
    "${ZEKIT_INC}"
    "${ZEKIT_INC3}"
    "${ZEKIT_FT}/include")

# ---- Compile definitions ----
# VK_NO_PROTOTYPES must be defined on EVERY translation unit in the library.
# With it, vulkan.h provides types and PFN_* types but no function prototypes,
# which is what lets ZeVulkanLoader.h declare variables that carry the same
# names as the Vulkan functions. Without it, whichever vulkan.h gets included
# first brings in real prototypes and collides with those definitions.
# PRIVATE, not PUBLIC: nothing in the public headers includes vulkan, and
# downstream should not inherit our implementation detail as a macro.
target_compile_definitions(zekit PRIVATE VK_NO_PROTOTYPES)

if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    target_compile_definitions(zekit PRIVATE
        NOMINMAX                      # otherwise the min/max macros in windows.h wreck std::min/std::max
        VK_USE_PLATFORM_WIN32_KHR     # without it there is no vkCreateWin32SurfaceKHR
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS       # portable C APIs such as getenv are flagged C4996 by MSVC
        _CRT_NONSTDC_NO_WARNINGS)
endif()

target_compile_features(zekit PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(zekit PRIVATE /utf-8 /MP)   # source files are UTF-8; /MP for parallel compiles
else()
    target_compile_options(zekit PRIVATE
        -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter)
endif()

# =====================================================================
#  Platform link surface (PUBLIC: linking zekit brings these along, so
#  downstream does not have to remember the list)
# =====================================================================
if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    target_link_libraries(zekit PUBLIC
        advapi32    # RegGetValueW (reads the system dark-mode setting)
        gdi32
        opengl32
        shlwapi
        user32
        winmm
        imm32       # input method
        gdiplus
        d3d12       # D3D12 device / command queue / fence
        dxgi        # swap chain (CreateDXGIFactory2)
        d3dcompiler # compiles HLSL at run time (D3DCompile)
        # Deliberately NOT linking dcomp.lib: DCompositionCreateDevice is
        # resolved at run time with LoadLibrary (see
        # ResolveDCompositionCreateDevice in ZeD3D12Context.cpp). An import
        # library dependency in a static library propagates to every consumer
        # - in practice a downstream project starts failing with "unresolved
        # external symbol DCompositionCreateDevice" after an upgrade. The DComp
        # interfaces are pure COM vtables, so this single exported function is
        # all that would need the import library; fetching it at run time is
        # simpler.
        ole32       # OleInitialize / file drag and drop (OLE)
        shell32     # DragAcceptFiles / IFileOpenDialog
        uuid)       # CLSID_FileOpenDialog / IID_IDropTarget

elseif(ZEKIT_PLATFORM STREQUAL "LINUX")
    # Dev packages: libx11-dev libgl-dev libegl-dev (libvulkan and the GPU driver
    # are only needed at run time)
    find_package(X11 REQUIRED)
    find_package(OpenGL QUIET)
    find_package(EGL QUIET)
    find_package(Threads REQUIRED)

    target_link_libraries(zekit PUBLIC X11::X11 Threads::Threads)
    if(TARGET OpenGL::GL)
        target_link_libraries(zekit PUBLIC OpenGL::GL)
    else()
        target_link_libraries(zekit PUBLIC GL)
    endif()
    if(TARGET EGL::EGL)
        target_link_libraries(zekit PUBLIC EGL::EGL)
    else()
        target_link_libraries(zekit PUBLIC EGL)
    endif()
    # -ldl: Vulkan is dlopen'ed at run time (ZeVulkanLoader), and EGL is the
    # fallback path for fetching GL 1.1 symbols
    target_link_libraries(zekit PUBLIC ${CMAKE_DL_LIBS})
    # Deliberately NOT linking libvulkan: Vulkan is dlopen'ed by
    # ZeVulkanLoader, so a machine without vulkan-1.dll / libvulkan.so falls
    # back to OpenGL instead of failing to start. Linking it would bring back
    # "the process dies when the library is missing".
    # Also do NOT add -DVK_USE_PLATFORM_XLIB_KHR globally: it makes vulkan.h
    # pull in <X11/Xlib.h>, leaking None / Status / Bool macros into every
    # translation unit. Only ZeVulkanContext.cpp needs it and it defines it in
    # its own file header.

elseif(ZEKIT_PLATFORM STREQUAL "ANDROID")
    target_link_libraries(zekit PUBLIC android log EGL GLESv3)

elseif(ZEKIT_PLATFORM STREQUAL "APPLE")
    target_link_libraries(zekit PUBLIC
        "-framework Metal" "-framework QuartzCore"
        "-framework Foundation" "-framework CoreGraphics")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(zekit PUBLIC "-framework UIKit")
    else()
        target_link_libraries(zekit PUBLIC "-framework AppKit")
    endif()
endif()

# =====================================================================
#  Output location
# =====================================================================
# Windows keeps the historical VS project layout x64/<Config>/Zekit.lib:
#   - downstream projects (the manual reader, the demos) look for the library
#     at that path;
#   - .gitignore has always excluded x64/.
if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    # The MT variant adds an "MT" suffix (x64/ReleaseMT). Different name, different
    # path: grabbing the wrong library is an LNK1104 rather than an LNK2038 in
    # the middle of the link.
    if(ZEKIT_STATIC_CRT)
        set(_zekit_out_suffix "MT")
    else()
        set(_zekit_out_suffix "")
    endif()
    set_target_properties(zekit PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY          "${ZEKIT_ROOT}/x64/$<CONFIG>${_zekit_out_suffix}"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG    "${ZEKIT_ROOT}/x64/Debug${_zekit_out_suffix}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE  "${ZEKIT_ROOT}/x64/Release${_zekit_out_suffix}"
        PDB_OUTPUT_DIRECTORY              "${ZEKIT_ROOT}/x64/$<CONFIG>${_zekit_out_suffix}")
endif()

# =====================================================================
#  Install (downstream can also use add_subdirectory or find_package)
# =====================================================================
include(GNUInstallDirs)
install(TARGETS zekit
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${ZEKIT_SRC}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")

# =====================================================================
#  Examples (optional): examples/gallery shows every widget in the library
# =====================================================================
#  Off by default: someone consuming the library wants the .lib and nothing
#  else, and the gallery costs two more translation units to build. But say so
#  out loud - silently not producing a program is how people end up staring at
#  an empty build directory wondering what went wrong.
option(ZEKIT_BUILD_EXAMPLES "Also build the programs under examples/ (the Control Gallery)" OFF)
if(ZEKIT_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
else()
    message(STATUS "Examples are NOT built. For the Control Gallery, configure with -DZEKIT_BUILD_EXAMPLES=ON")
endif()

# =====================================================================
#  Summary: state where the artifacts land, so nobody has to go hunting
# =====================================================================
message(STATUS "Zekit ${PROJECT_VERSION} - platform ${ZEKIT_PLATFORM}")
if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    message(STATUS "  library : ${ZEKIT_ROOT}/x64/<Config>${_zekit_out_suffix}/Zekit.lib")
else()
    message(STATUS "  library : ${CMAKE_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}Zekit${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
if(ZEKIT_BUILD_EXAMPLES)
    message(STATUS "  gallery : ${CMAKE_BINARY_DIR}/bin/ZekitGallery${CMAKE_EXECUTABLE_SUFFIX}")
endif()
