# =====================================================================
#  Zekit source list - the single source of truth for every platform
# =====================================================================
#  Why this file exists:
#    The same list used to be maintained by hand in four places (the
#    <ClCompile> block of Zekit.vcxproj, SRCS in build_zekit.sh, the glob+skip
#    in build_zekit_linux.sh, and the android/apple CMakeLists). The cost of
#    forgetting one was never a compile error - it was "the library is missing
#    a batch of symbols and downstream fails with LNK2001".
#
#  There are now only two rules:
#    1. Cross-platform files are not registered anywhere - Zekit/*.cpp is
#       globbed, so a new file joins the build by itself;
#    2. Platform-only files are registered once, in one of the four tables
#       below. The criterion is "this file does not compile on other
#       platforms", not "it belongs to feature X".
#
#  WARNING - order of questions when adding a file (this matters):
#    First ask: does it have a file-level platform guard? If yes (e.g.
#    #if defined(__OHOS__) in ZeWindowHarmony.cpp) it still belongs in the
#    table: it saves a pointless compile on other platforms and keeps "which
#    files belong to other platforms" visible in one place.
#    If no (e.g. ZeRenderBackendAndroid.cpp, which includes <android/...> right
#    away), it MUST be registered, otherwise the other platforms simply fail to
#    build.
# =====================================================================

set(ZEKIT_SRC  "${CMAKE_CURRENT_LIST_DIR}/../Zekit")
set(ZEKIT_INC  "${CMAKE_CURRENT_LIST_DIR}/../inc")
set(ZEKIT_INC3 "${CMAKE_CURRENT_LIST_DIR}/../inc/Include")
set(ZEKIT_FT   "${ZEKIT_SRC}/third_party/freetype-2.14.1")

# ---------------------------------------------------------------------
#  Platform-only .cpp files (names relative to Zekit/)
# ---------------------------------------------------------------------
#  A given feature has exactly one implementation per platform, so these files
#  belong to one platform each.
#  ZeEglContext / ZeSoilCompat belong to both Linux and Android (EGL is their
#  common window system, and ZeSoilCompat replaces SOIL2.c on both - the same
#  symbols cannot be compiled twice).
set(ZEKIT_PLATFORM_FILES_WINDOWS
    ZeWindowWin32.cpp
    ZeGLContext.cpp
    ZeRenderBackendWin32.cpp
    ZeD3D12Context.cpp
    ZeD3D12UIRenderer.cpp
)
set(ZEKIT_PLATFORM_FILES_LINUX
    ZeWindowX11.cpp
    ZeEglContext.cpp
    ZeSoilCompat.cpp
    ZeRenderBackendLinux.cpp
)
set(ZEKIT_PLATFORM_FILES_ANDROID
    ZeWindowAndroid.cpp
    ZeRenderBackendAndroid.cpp
    ZeEglContext.cpp
    ZeSoilCompat.cpp
)
set(ZEKIT_PLATFORM_FILES_HARMONY
    ZeWindowHarmony.cpp
    ZeRenderBackendHarmony.cpp
)
set(ZEKIT_PLATFORM_FILES_APPLE
    ZeWindowApple.cpp
    ZeRenderBackendApple.mm
    ZeMetalContext.mm
    ZeMetalUIRenderer.mm
)

# Every file "owned" by some platform - used to derive who this platform excludes
set(_ZEKIT_ALL_PLATFORM_FILES
    ${ZEKIT_PLATFORM_FILES_WINDOWS}
    ${ZEKIT_PLATFORM_FILES_LINUX}
    ${ZEKIT_PLATFORM_FILES_ANDROID}
    ${ZEKIT_PLATFORM_FILES_HARMONY}
    ${ZEKIT_PLATFORM_FILES_APPLE}
)

# ---------------------------------------------------------------------
#  zekit_collect_sources(<PLATFORM> <out_var>)
# ---------------------------------------------------------------------
#  PLATFORM is one of WINDOWS / LINUX / ANDROID / HARMONY / APPLE.
#  Returns the absolute path list to compile for that platform in <out_var>
#  (PARENT_SCOPE).
#
#  Implementation = "glob everything - other platforms' files + own files".
#  CONFIGURE_DEPENDS is used on purpose: dropping a new .cpp into Zekit/ is
#  picked up by the next build without rerunning cmake by hand.
function(zekit_collect_sources PLATFORM out_var)
    set(_keep ${ZEKIT_PLATFORM_FILES_${PLATFORM}})
    if(NOT _keep)
        message(FATAL_ERROR "Unknown platform '${PLATFORM}': expected WINDOWS / LINUX / ANDROID / HARMONY / APPLE")
    endif()

    file(GLOB _cpp CONFIGURE_DEPENDS "${ZEKIT_SRC}/*.cpp")

    set(_result "")
    foreach(_f IN LISTS _cpp)
        get_filename_component(_name "${_f}" NAME)

        # (1) the file belongs to some platform and this is not it -> skip
        list(FIND _ZEKIT_ALL_PLATFORM_FILES "${_name}" _idx)
        if(_idx GREATER -1 AND NOT _name IN_LIST _keep)
            continue()
        endif()

        # (2) everything else is cross-platform (or belongs to this platform) -> keep
        list(APPEND _result "${_f}")
    endforeach()

    # Names already collected (used to tell whether a platform file came in via the glob)
    set(_result_names "")
    foreach(_f IN LISTS _result)
        get_filename_component(_n "${_f}" NAME)
        list(APPEND _result_names "${_n}")
    endforeach()

    # (3) this platform's files must actually exist - renaming a file must not
    #     silently turn into "one file fewer to compile" - and must be added when
    #     the glob cannot see them (Apple's .mm files, since only *.cpp is globbed)
    foreach(_name IN LISTS _keep)
        if(NOT EXISTS "${ZEKIT_SRC}/${_name}")
            message(FATAL_ERROR
                "The ${PLATFORM} list names '${_name}', but ${ZEKIT_SRC}/${_name} does not exist.\n"
                "  -> after renaming or deleting a file, update the platform tables in cmake/ZekitSources.cmake.")
        endif()
        if(NOT _name IN_LIST _result_names)
            list(APPEND _result "${ZEKIT_SRC}/${_name}")
        endif()
    endforeach()

    list(LENGTH _result _n)
    message(STATUS "Zekit sources (${PLATFORM}): ${_n} .cpp files")
    set(${out_var} ${_result} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
#  zekit_freetype_sources(<out_var>)
# ---------------------------------------------------------------------
#  The 43 FreeType 2.14.1 modules to compile, following the official
#  docs/INSTALL.ANY ("DO NOT COMPILE ALL C FILES! Rather, compile the following
#  ones.").
#
#  ftsystem.c is split by platform: Windows uses the builds/windows/ copy (it
#  goes through the Win32 file API), every other platform uses the generic one
#  in src/base/.
#  ftgzip.c carries its own zlib (inflate / adler32 / crc32 are all inside it),
#  so no external zlib is needed.
#  Leaving one module out does not fail at compile time - it fails at link time
#  with undefined references. After editing this table, actually link something.
function(zekit_freetype_sources out_var)
    set(_mods
        base/ftbase.c base/ftinit.c base/ftdebug.c
        base/ftbbox.c base/ftglyph.c base/ftbitmap.c base/ftmm.c base/ftstroke.c
        base/ftsynth.c base/ftfstype.c base/fttype1.c base/ftgasp.c base/ftgxval.c
        base/ftotval.c base/ftcid.c base/ftpfr.c base/ftwinfnt.c base/ftbdf.c
        base/ftpatent.c
        autofit/autofit.c truetype/truetype.c type1/type1.c cff/cff.c
        cid/type1cid.c pfr/pfr.c type42/type42.c winfonts/winfnt.c pcf/pcf.c
        bdf/bdf.c
        psaux/psaux.c psnames/psnames.c pshinter/pshinter.c sfnt/sfnt.c
        smooth/smooth.c raster/raster.c sdf/sdf.c svg/svg.c
        gzip/ftgzip.c lzw/ftlzw.c
        gxvalid/gxvalid.c otvalid/otvalid.c
        cache/ftcache.c
    )

    if(WIN32)
        set(_ftsystem "${ZEKIT_FT}/builds/windows/ftsystem.c")
    else()
        set(_ftsystem "${ZEKIT_FT}/src/base/ftsystem.c")
    endif()

    set(_out "${_ftsystem}")
    foreach(_m IN LISTS _mods)
        list(APPEND _out "${ZEKIT_FT}/src/${_m}")
    endforeach()

    list(LENGTH _out _n)
    if(NOT _n EQUAL 43)
        message(WARNING "FreeType module count is ${_n} (expected 43) - check the _mods table in cmake/ZekitSources.cmake")
    endif()

    set(${out_var} ${_out} PARENT_SCOPE)
endfunction()
