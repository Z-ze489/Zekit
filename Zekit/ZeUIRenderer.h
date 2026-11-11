#pragma once
/**
 * @file ZeUIRenderer.h
 * @brief GL version of the UI renderer (counterpart of `Vk::VkUIRenderer`)
 *
 * @details
 * Consumes a `Kit::FrameCollector` (pure display list), pushes vertices/indices to the GPU and draws.
 * The widget layer, layout and input need not change a single character -- the `FrameCollector`, the "de facto
 * RHI", already left the seam in place.
 *
 * ## Platform independence
 *
 * This class does not hold or know about the window system: it only requires that "when `Init()` / `Draw()` is called,
 * a current GL context already exists", and everything else is `gl*` calls.
 *
 * This is possible because the original `Init(GL::GLContext&)` borrowed only one thing from
 * `GLContext::createProgram()`, and that thing has been moved into the platform-independent
 * `ZeGLProgram.h`. Thus the same implementation is shared by two contexts:
 *
 * | Platform | Context | Who does MakeCurrent / SwapBuffers |
 * |---|---|---|
 * | Windows | `GL::GLContext` (WGL) | `ZeRenderBackendWin32.cpp` |
 * | Linux | `GL::EglContext` (EGL) | `ZeRenderBackendLinux.cpp` |
 */

#include <cstdint>

namespace Kit {
	class FrameCollector;

	class UIRenderer {
	public:
		UIRenderer() = default;
		~UIRenderer();

		UIRenderer(const UIRenderer&) = delete;
		UIRenderer& operator=(const UIRenderer&) = delete;

  /// @brief Creates program / VAO / VBO
  /// @pre A current GL context must exist when called (this class does no context management)
  /// @return false = shader compilation or linking failed
		bool Init();

  /// @brief Releases all GL objects
  /// @pre A current GL context must exist when called (otherwise objects of another context are deleted)
		void Shutdown();

  /// @brief Per frame: upload vertices + draw (must be in a current context and viewport already set)
		void Draw(const FrameCollector& fc, int fbWidth, int fbHeight);
	private:
		unsigned int prog_ = 0;        
		unsigned int vao_ = 0;
		unsigned int vbo_ = 0;
		long long    vboCapacity_ = 0;  
		int          locProj_ = -1;    

		int          locTex_ = -1;   // Location of sampler2D uTex
		int          locIsTex_ = -1;   // Location of int uIsTex
		int          locIsColor_ = -1;   // Location of int uIsColor (color glyphs: emoji)
	};
}