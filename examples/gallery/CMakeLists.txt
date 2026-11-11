# =====================================================================
#  Zekit Control Gallery —— 示例程序
# =====================================================================
#  一个窗口把 Zekit 的全部控件摆出来：长什么样、来自哪个头文件、常用 setter
#  怎么调。它同时是"这个库能干什么"的说明书，和最快的冒烟测试。
#
#  怎么编（不需要先编库，本文件会把 ../.. 作为子目录带进来）：
#      cmake -S examples/gallery -B build/gallery
#      cmake --build build/gallery -j
#      Windows: build/gallery/Debug/ZekitGallery.exe
#      Linux  : build/gallery/ZekitGallery
#
#  在仓库根目录编库时一并带上示例：
#      cmake --preset windows-msvc -DZEKIT_BUILD_EXAMPLES=ON
# =====================================================================
cmake_minimum_required(VERSION 3.22)
project(ZekitGallery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 从仓库根目录进来时 zekit 目标已存在；单独编本目录时把库源码带进来。
if(NOT TARGET zekit)
    set(ZEKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                     "${CMAKE_BINARY_DIR}/zekit")
endif()

# 库用静态 CRT（/MT）时，本示例必须跟着用 —— 两种 CRT 的目标文件不能混链。
# 单独编本目录时（上面那段 add_subdirectory）顶层设的变量不会回流到这里，
# 所以这里再补一次；必须在第一个 target() 之前。
if(MSVC AND ZEKIT_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(ZekitGallery
    main.cpp
    Demo.cpp)

target_link_libraries(ZekitGallery PRIVATE zekit)

if(MSVC)
    target_compile_options(ZekitGallery PRIVATE /utf-8)
else()
    target_compile_options(ZekitGallery PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# 单文件 exe（不要求用户装 VC++ 运行时的）用静态 CRT 库：
#   cmake --preset windows-msvc-mt && cmake --build --preset windows-msvc-mt-release
#   然后 -DZEKIT_STATIC_CRT=ON 重编本示例即可。
