# =====================================================================
#  Zekit —— 统一 CMake 构建
# =====================================================================
#  产物（一个**自包含静态库**：FreeType / SOIL2 / glad 都编在库里，
#  下游只链一个文件，不装 Vulkan SDK、不装 FreeType 就能编）：
#      Windows : x64/<Config>/Zekit.lib      （路径刻意与旧 VS 工程一致）
#      Linux   : build/<dir>/libZekit.a
#      Android : libzekit.a + libmain.so（见 android/）
#      Apple   : libzekit_apple.a（见 apple/）
#
#  怎么用（Windows / VS 2026 工具链）：
#      cmake --preset windows-msvc-release
#      cmake --build --preset windows-msvc-release          # → x64/Release/Zekit.lib
#
#  门面工程（要**单文件 exe**、不能要求用户装 VC++ 运行时的）走静态 CRT 变体：
#      cmake --preset windows-msvc-mt
#      cmake --build --preset windows-msvc-mt-release       # → x64/ReleaseMT/Zekit.lib
#      ★ 两种 CRT 的目标文件不可混链（见下方「运行时库变体」一节）。
#
#  怎么用（Linux）：
#      cmake --preset linux-release && cmake --build --preset linux-release
#
#  为什么不再用脚本：同一份源文件清单以前要人工维护在 4 个地方（vcxproj /
#  build_zekit.sh / build_zekit_linux.sh / android+apple 的 CMakeLists），
#  漏一处不报错、只是库少一批符号，下游 LNK2001（源码地图 §十七 记过一次）。
#  现在清单只有 cmake/ZekitSources.cmake 一处：跨平台文件 glob 自动进，
#  平台专属文件登记一张表。
# =====================================================================
cmake_minimum_required(VERSION 3.22)

project(Zekit
    VERSION 1.0.0
    DESCRIPTION "C++20 桌面 GUI 控件库（自带窗口 / 渲染 / 控件体系）"
    LANGUAGES C CXX)

# 苹果的 Metal 实现是 .mm（Objective-C++）
if(APPLE AND NOT ANDROID)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)     # 用 -std=c++20，不用 gnu++20（与旧脚本一致）

# ---- 目录约定 ----
set(ZEKIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${ZEKIT_ROOT}/cmake/ZekitSources.cmake")

# ---- 平台判定（ANDROID 必须先于 UNIX 判：它是 UNIX）----
if(WIN32)
    set(ZEKIT_PLATFORM WINDOWS)
elseif(ANDROID)
    set(ZEKIT_PLATFORM ANDROID)
elseif(APPLE)
    set(ZEKIT_PLATFORM APPLE)
elseif(UNIX)
    set(ZEKIT_PLATFORM LINUX)
else()
    message(FATAL_ERROR "Zekit 目前支持 Windows / Linux / Android / 鸿蒙 / 苹果，当前平台未适配")
endif()

# =====================================================================
#  运行时库（CRT）变体
# =====================================================================
#  为什么要有两个：
#    · **开源默认 /MD**（MultiThreadedDLL）—— 库使用方（第三方项目）的常规选择，
#      目标文件小、与系统共享 CRT、升级运行时不必重编；
#    · **门面 /MT**（MultiThreaded）—— Zekit Guide / Demo 是"发出去给人双击就开"的
#      单文件 exe，不能要求用户先装 VC++ 运行时。
#
#  ★ 两种 CRT 的目标文件**绝对不能混链**。混了就是：
#      LNK2038 "RuntimeLibrary 不匹配: MD_DynamicRelease vs MT_StaticRelease"
#      + 一堆 LNK2001 `__imp_lround` / `__imp_freopen` / `__imp___stdio_common_vsscanf`
#      （`__imp_` 前缀 = 库在找 DLL 版 CRT，而你按静态 CRT 在链）。
#    所以 ON 时产物落到 **x64/<Config>MT/**，与开源版既不同名也不同路径 ——
#    谁拿错库，链接器会立刻报 LNK1104 打不开文件，而不是拖到链接期才炸。
#
#  用法：
#      cmake --preset windows-msvc-mt          # → x64/ReleaseMT/Zekit.lib
#      cmake --build --preset windows-msvc-mt-release
option(ZEKIT_STATIC_CRT
    "用静态 CRT（/MT）构建，产物落 x64/<Config>MT（门面工程 Guide / Demo 用）" OFF)

if(MSVC)
    if(ZEKIT_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    # ★ 必须写在**第一个 target()** 之前：该变量只在目标创建时被读取一次。
endif()

# =====================================================================
#  第三方：FreeType / SOIL2 / glad
# =====================================================================
#  三者都编成 **OBJECT 库**，再把目标文件塞进 zekit 静态库本体 ——
#  这是"下游只链一个 Zekit.lib"这条性质的实现方式（旧脚本靠 lib.exe 手工合并
#  .obj 达到同样效果）。用 OBJECT 而不是 STATIC，是因为静态库不会把依赖库
#  吸进自己：目标文件只有真的进了归档，才叫自包含。

zekit_freetype_sources(ZEKIT_FT_SOURCES)
add_library(zekit_freetype OBJECT ${ZEKIT_FT_SOURCES})
# 头文件顺序：宿主 inc 在前（决定 ftoption / ftmodule 的配置），源码树 include
# 在后（提供 freetype/internal/，编模块必需 —— 宿主只发了公开头）。
target_include_directories(zekit_freetype PRIVATE
    "${ZEKIT_INC}"
    "${ZEKIT_FT}/include")
target_compile_definitions(zekit_freetype PRIVATE FT2_BUILD_LIBRARY)
if(MSVC)
    target_compile_definitions(zekit_freetype PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    target_compile_options(zekit_freetype PRIVATE /w)     # 第三方代码，别跟我们的告警标准
else()
    target_compile_options(zekit_freetype PRIVATE -w)
endif()

# SOIL2：图片解码。⚠️ 与 ZeSoilCompat.cpp 是**同名符号的二选一**：
#   Linux 只有 ZeSoilCompat（不编 SOIL2.c —— 那边要 GLX 头，而 ImageStore 只用
#   三个纯 CPU 函数）；其余平台编 SOIL2.c（Android 走它自己的那份，见 android/）。
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
#  Zekit 本体：Plat 窗口层 + Kit 控件层 + Rhi 渲染层
# =====================================================================
zekit_collect_sources(${ZEKIT_PLATFORM} ZEKIT_SOURCES)

add_library(zekit STATIC ${ZEKIT_SOURCES})
add_library(zekit::zekit ALIAS zekit)

set_target_properties(zekit PROPERTIES
    OUTPUT_NAME "Zekit"                  # 大小写与旧产物一致：Zekit.lib
    POSITION_INDEPENDENT_CODE ON)        # Linux 上要能被共享库/可执行文件共用

target_sources(zekit PRIVATE $<TARGET_OBJECTS:zekit_freetype>)
if(TARGET zekit_soil2)
    target_sources(zekit PRIVATE $<TARGET_OBJECTS:zekit_soil2>)
endif()

# glad.c 是 **C** 代码（大量 void* 隐式转换在 C++ 里非法）。
# CMake 按扩展名选编译器，所以直接丢进源列表即可；C++ 侧符号能对上，
# 是因为 glad.h 用 extern "C" 包住了声明。
# ★ Android 用系统 GLES3 头（直接函数，无需加载器），苹果走 Metal —— 都不带 glad。
if(NOT ZEKIT_PLATFORM STREQUAL "ANDROID" AND NOT ZEKIT_PLATFORM STREQUAL "APPLE")
    target_sources(zekit PRIVATE "${ZEKIT_SRC}/lib/glad.c")
endif()

# ---- 头文件搜索面 ----
# PUBLIC：下游只需 -I Zekit/include 就能 #include <Zekit.h>（旧约定不变）
# PRIVATE：库内实现需要 ① 内部头目录（"include/xxx.h" 这种写法的相对基准）
#          ② 第三方头（freetype / SOIL2 / glad / glm / vulkan / vma）
target_include_directories(zekit PUBLIC
    "${ZEKIT_SRC}/include")
target_include_directories(zekit PRIVATE
    "${ZEKIT_SRC}"
    "${ZEKIT_INC}"
    "${ZEKIT_INC3}")

# ---- 编译定义 ----
# VK_NO_PROTOTYPES 必须加在**所有**库内 TU 上：vulkan.h 在这种情况下只给类型 +
# PFN_*，不给函数原型，ZeVulkanLoader.h 里那批"与函数同名的变量"才有立足之地。
# 漏了它的症状是"先被 include 的那份 vulkan.h 带来函数声明，与变量定义冲突"。
# ★ 是 PRIVATE 不是 PUBLIC：公开头（include/）里**一个字都不 include vulkan**，
#   下游不该被我们的实现细节绑上这个宏。
target_compile_definitions(zekit PRIVATE VK_NO_PROTOTYPES)

if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    target_compile_definitions(zekit PRIVATE
        NOMINMAX                      # 不定义的话 windows.h 的 min/max 宏会打烂 std::min/std::max
        VK_USE_PLATFORM_WIN32_KHR     # 不加就没有 vkCreateWin32SurfaceKHR
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS       # getenv 等可移植 C API 在 MSVC 上被标 C4996
        _CRT_NONSTDC_NO_WARNINGS)
endif()

target_compile_features(zekit PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(zekit PRIVATE /utf-8 /MP)   # 中文注释；/MP 并行编译
else()
    target_compile_options(zekit PRIVATE
        -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter)
endif()

# =====================================================================
#  平台链接面（PUBLIC：下游链 zekit 就自动带上，不必自己记一长串系统库）
# =====================================================================
if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    target_link_libraries(zekit PUBLIC
        advapi32    # RegGetValueW（读系统深色模式）
        gdi32
        opengl32
        shlwapi
        user32
        winmm
        imm32       # 输入法
        gdiplus
        d3d12       # D3D12 设备 / 命令队列 / 围栏
        dxgi        # 交换链（CreateDXGIFactory2）
        d3dcompiler # 运行时编译 HLSL（D3DCompile）
        # ★ 不链 dcomp.lib：`DCompositionCreateDevice` 改成运行时 LoadLibrary 解析
        #   （见 ZeD3D12Context.cpp 的 ResolveDCompositionCreateDevice）。
        #   静态库的 import lib 依赖会传染给每个下游 —— 实测下游工程一升级就报
        #   "无法解析的外部符号 DCompositionCreateDevice"。DComp 接口都是纯 COM
        #   vtable，只有这一个导出函数需要 import lib，运行时取最省事。
        ole32       # OleInitialize / 文件拖放（OLE）
        shell32     # DragAcceptFiles / IFileOpenDialog
        uuid)       # CLSID_FileOpenDialog / IID_IDropTarget

elseif(ZEKIT_PLATFORM STREQUAL "LINUX")
    # 开发包：libx11-dev libgl-dev libegl-dev（运行期才需要 libvulkan / 显卡驱动）
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
    # -ldl：Vulkan 运行期 dlopen（ZeVulkanLoader）+ EGL 兜底取 GL 1.1 符号
    target_link_libraries(zekit PUBLIC ${CMAKE_DL_LIBS})
    # ⚠️ 刻意**不链** libvulkan：Vulkan 由 ZeVulkanLoader 运行期 dlopen，
    #    缺 vulkan-1.dll / libvulkan.so 时优雅回退 OpenGL；链上它就等于把
    #    "缺库时进程直接起不来"重新钉回来。
    # ⚠️ 也**不要**全局加 -DVK_USE_PLATFORM_XLIB_KHR：那会让 vulkan.h 顺带
    #    include <X11/Xlib.h>，把 None / Status / Bool 等宏泄进每个翻译单元。
    #    需要它的只有 ZeVulkanContext.cpp，它自己在文件头定义。

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
#  产物位置
# =====================================================================
#  Windows 刻意沿用 VS 工程的输出路径 x64/<Config>/Zekit.lib：
#    · 下游工程（Zekit Guide 等）与 Zekit/tests 下的探针脚本都按这个路径找库；
#    · .gitignore 早就忽略了 x64/。
if(ZEKIT_PLATFORM STREQUAL "WINDOWS")
    # MT 变体加 "MT" 后缀（x64/ReleaseMT）。名与路径都不同 —— 拿错库时
    # 是 LNK1104 打不开文件，而不是链接到一半才报 LNK2038。
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
#  安装（下游也可以用 add_subdirectory / find_package 接）
# =====================================================================
include(GNUInstallDirs)
install(TARGETS zekit
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${ZEKIT_SRC}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")

# =====================================================================
#  示例（可选）：examples/gallery = 全部控件的展示程序
# =====================================================================
option(ZEKIT_BUILD_EXAMPLES "同时构建 examples/ 下的示例程序" OFF)
if(ZEKIT_BUILD_EXAMPLES)
    add_subdirectory(examples/gallery)
endif()

message(STATUS "Zekit ${PROJECT_VERSION} — 平台 ${ZEKIT_PLATFORM}，产物名 ${CMAKE_STATIC_LIBRARY_PREFIX}Zekit${CMAKE_STATIC_LIBRARY_SUFFIX}")
