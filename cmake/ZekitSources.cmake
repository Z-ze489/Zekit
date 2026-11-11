# =====================================================================
#  Zekit 源文件清单 —— **所有平台共用的唯一来源**
# =====================================================================
#  为什么要有这个文件：
#    以前同一份清单要人工维护在四个地方（Zekit.vcxproj 的 <ClCompile>、
#    build_zekit.sh 的 SRCS、build_zekit_linux.sh 的 glob+skip、
#    android/apple 的 CMakeLists）。漏登记一处的代价不是编译报错，而是
#    "库少一批符号、下游 LNK2001"（源码地图 §十七 / §二十二 记过两次）。
#
#  现在只有两条规则：
#    ① **跨平台文件不用登记** —— 直接 glob `Zekit/*.cpp`，新增文件自动进构建；
#    ② **平台专属文件登记一次**，写进下面四张表之一。判据是"这个文件在别的
#       平台上编不过"，而不是"它属于哪个功能模块"。
#
#  ⚠️ 加新文件的判断顺序（很重要）：
#    先问"它有没有文件级平台守卫？"—— 有（如 ZeWindowHarmony.cpp 的
#    `#if defined(__OHOS__)`）就仍然要登记进表里：省一次无意义的编译，也让
#    "哪些文件属于别的平台"在这张表上一眼看全；
#    没有（如 ZeRenderBackendAndroid.cpp，一上来就 include <android/...>）则
#    **必须**登记，否则会在其它平台上直接编译失败。
# =====================================================================

set(ZEKIT_SRC  "${CMAKE_CURRENT_LIST_DIR}/../Zekit")
set(ZEKIT_INC  "${CMAKE_CURRENT_LIST_DIR}/../inc")
set(ZEKIT_INC3 "${CMAKE_CURRENT_LIST_DIR}/../inc/Include")
set(ZEKIT_FT   "${ZEKIT_SRC}/third_party/freetype-2.14.1")

# ---------------------------------------------------------------------
#  平台专属 .cpp（相对 Zekit/ 的文件名）
# ---------------------------------------------------------------------
#  ★ 同一个功能在不同平台上必然是"一份实现"，所以这些文件**只属于一个平台**。
#  ★ ZeEglContext / ZeSoilCompat 同时属于 Linux 与 Android（EGL 是两者的共同
#    窗口系统；ZeSoilCompat 在两者都替代 SOIL2.c —— 同名符号不能同时编）。
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

# 所有"被某个平台独占"的文件 —— 用来反推"本平台该排除谁"
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
#  PLATFORM ∈ WINDOWS / LINUX / ANDROID / HARMONY / APPLE
#  返回：该平台要参与编译的**绝对路径**列表，写进 <out_var>（PARENT_SCOPE）。
#
#  实现 = "全量 glob − 别人的专属文件 + 自己的专属文件"。
#  ★ 用 CONFIGURE_DEPENDS：往 Zekit/ 里丢一个新 .cpp 时，下次 build 会自动
#    重新 glob（不必手动跑 cmake）。
function(zekit_collect_sources PLATFORM out_var)
    set(_keep ${ZEKIT_PLATFORM_FILES_${PLATFORM}})
    if(NOT _keep)
        message(FATAL_ERROR "未知平台 '${PLATFORM}'：可选 WINDOWS / LINUX / ANDROID / HARMONY / APPLE")
    endif()

    file(GLOB _cpp CONFIGURE_DEPENDS "${ZEKIT_SRC}/*.cpp")

    set(_result "")
    foreach(_f IN LISTS _cpp)
        get_filename_component(_name "${_f}" NAME)

        # ① 文件属于"某个平台"，但不是本平台 → 排除
        list(FIND _ZEKIT_ALL_PLATFORM_FILES "${_name}" _idx)
        if(_idx GREATER -1 AND NOT _name IN_LIST _keep)
            continue()
        endif()

        # ② 剩下的都是跨平台文件（或本平台的专属文件）→ 收
        list(APPEND _result "${_f}")
    endforeach()

    # 已收文件的文件名（用来判断"平台专属文件是否已经被 glob 带上"）
    set(_result_names "")
    foreach(_f IN LISTS _result)
        get_filename_component(_n "${_f}" NAME)
        list(APPEND _result_names "${_n}")
    endforeach()

    # ③ 本平台的专属文件必须真实存在（防止改名后静默变成"少编一个"），
    #    并且补上 glob 扫不到的那种（苹果的 .mm —— 上面只 glob 了 *.cpp）
    foreach(_name IN LISTS _keep)
        if(NOT EXISTS "${ZEKIT_SRC}/${_name}")
            message(FATAL_ERROR
                "平台 ${PLATFORM} 的清单里有 '${_name}'，但 ${ZEKIT_SRC}/${_name} 不存在。\n"
                "  → 文件改名/删除后记得同步 cmake/ZekitSources.cmake 的平台表。")
        endif()
        if(NOT _name IN_LIST _result_names)
            list(APPEND _result "${ZEKIT_SRC}/${_name}")
        endif()
    endforeach()

    list(LENGTH _result _n)
    message(STATUS "Zekit 源文件（${PLATFORM}）：${_n} 个 .cpp")
    set(${out_var} ${_result} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
#  zekit_freetype_sources(<out_var>)
# ---------------------------------------------------------------------
#  FreeType 2.14.1 要编的 43 个模块，清单以官方 docs/INSTALL.ANY 为准
#  （原文："DO NOT COMPILE ALL C FILES! Rather, compile the following ones."）。
#
#  ★ ftsystem.c 平台分流：Windows 用 builds/windows/ 那份（走 Win32 文件 API），
#    其余平台用 src/base/ 的通用实现。
#  ★ ftgzip.c **自带 zlib**（inflate / adler32 / crc32 都在里面），不需要额外 zlib。
#  ★ 少编一个不会在编译期报错 —— 只会在链接期报 undefined reference，
#    所以改这张表之后必须真的链接一次。
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
        message(WARNING "FreeType 模块数 ${_n}（应为 43）—— 检查 cmake/ZekitSources.cmake 的 _mods 表")
    endif()

    set(${out_var} ${_out} PARENT_SCOPE)
endfunction()
