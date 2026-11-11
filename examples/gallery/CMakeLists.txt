# =====================================================================
#  Zekit Control Gallery - example program
# =====================================================================
#  One window that puts every Zekit widget on screen: what it looks like, which
#  header it comes from, how the common setters are used. It doubles as the
#  "what can this library do" document and as the fastest smoke test.
#
#  Build (no need to build the library first, this file pulls ../.. in):
#      cmake -S examples/gallery -B build/gallery
#      cmake --build build/gallery -j
#      Windows: build/gallery/Debug/ZekitGallery.exe
#      Linux  : build/gallery/ZekitGallery
#
#  To build it together with the library from the repository root:
#      cmake --preset windows-msvc -DZEKIT_BUILD_EXAMPLES=ON
# =====================================================================
cmake_minimum_required(VERSION 3.22)
project(ZekitGallery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# When configured from the repository root the zekit target already exists.
# When this directory is configured on its own, pull the library sources in.
if(NOT TARGET zekit)
    set(ZEKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.."
                     "${CMAKE_BINARY_DIR}/zekit")
endif()

# If the library is built with the static CRT (/MT), this program must use it
# too - objects from the two CRTs cannot be linked together.
# The variable set by the root project does not flow back up here when this
# directory is configured on its own, so set it again; it has to happen before
# the first target() call.
if(MSVC AND ZEKIT_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(ZekitGallery
    main.cpp
    Demo.cpp)

target_link_libraries(ZekitGallery PRIVATE zekit)

# Always land in <build>/bin/ZekitGallery[.exe], never in a per-config
# subdirectory and never inside the source tree. Multi-config generators
# (Visual Studio) otherwise append Release/ or Debug/, and the "where is my
# exe" question gets a different answer depending on which generator you used.
set_target_properties(ZekitGallery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(ZekitGallery PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    target_compile_options(ZekitGallery PRIVATE /utf-8)
else()
    target_compile_options(ZekitGallery PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

message(STATUS "ZekitGallery -> ${CMAKE_BINARY_DIR}/bin/ZekitGallery${CMAKE_EXECUTABLE_SUFFIX}")

# For a single-file exe that starts on a machine without the VC++ runtime, build
# the library and this program with the static CRT:
#   cmake --preset windows-msvc-mt -DZEKIT_BUILD_EXAMPLES=ON
#   cmake --build --preset windows-msvc-mt-release
