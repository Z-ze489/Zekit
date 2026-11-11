#pragma once
#include <Windows.h>
#include <glad/glad.h>

namespace GL {

 /// Which GPU the current OpenGL context **actually** lands on (used on dual-GPU machines to verify whether the discrete GPU is really selected)
	struct RendererInfo {
		const char* vendor = "";      /// < GL_VENDOR, e.g. "NVIDIA Corporation" / "Intel"
		const char* renderer = "";    /// < GL_RENDERER, e.g. "NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2"
		const char* version = "";     /// < GL_VERSION, e.g. "4.6.0 NVIDIA 560.94"
		bool isDiscrete = false;      /// < Determines whether it is a discrete GPU by vendor/renderer keywords
	};

	class GLContext {
	public:
		GLContext() = default;
		~GLContext();

  // Copying forbidden, moving allowed
		GLContext(const GLContext&) = delete;
		GLContext& operator=(const GLContext&) = delete;
		GLContext(GLContext&&) noexcept;
		GLContext& operator=(GLContext&&) noexcept;

        // Attaches the context to the window (calls GetDC internally)
        // major/minor default to 4.6, core profile
        bool Create(HWND hwnd, int major = 4, int minor = 6,
            bool coreProfile = true);

        /// @brief Creates a context in the **same share group** as `share` and attaches it to `hwnd`
        /// @param hwnd   New window (a child window or another top-level window both work)
        /// @param share  Existing context; the new context shares **named objects** with it
        /// @return false = creation failed (the caller should fall back to a standalone context rather than continue with an empty RC)
        ///
        /// @details
        /// Goes through the `hShareContext` parameter of `wglCreateContextAttribsARB` -- this is the only
        /// sharing entry point in WGL, **there is no second way** (`wglShareLists` is the older API before it, which requires
        /// that neither context has ever been made current, and has more restrictions).
        ///
        /// @par What is shared
        /// Per the OpenGL spec, a share group **shares**: textures, buffer objects (VBO/EBO),
        /// shader and program objects, renderbuffers, samplers, sync objects.
        /// **Not shared**: vertex array objects (VAO), framebuffer objects (FBO), and "current binding state".
        ///
        /// @par A hard prerequisite
        /// Both contexts must land on the **same pixel format**, otherwise sharing silently fails (creation returns
        /// non-null but `glIsTexture` cannot find the other side's texture). This class's `Create` / `CreateShared`
        /// use the same set of `pixelAttribs`, and `wglChoosePixelFormatARB` is deterministic --
        /// so on the same machine the format is necessarily the same. **Anyone changing the pixel format must be aware of this dependency.**
        ///
        /// @note `share` need not be current before the call -- the parameter passed is its HGLRC.
        /// @note This function does **not** redo the discrete GPU switch (`RequestDiscreteGPU`): that is a process-level one-time
        ///       action, and should already have been done when `share` was created.
        bool CreateShared(HWND hwnd, const GLContext& share,
            int major = 4, int minor = 6, bool coreProfile = true);

        // Activates/deactivates
        void MakeCurrent();
        void DoneCurrent();

        // Swaps buffers (needs the window's HDC, which is actually the internal m_hDC)
        void SwapBuffers();

        /// @brief Sets the swap interval: 0 = do not wait for vertical blank, 1 = wait (default)
        /// @note Goes through `wglSwapIntervalEXT`. **Silently does nothing when the extension is unsupported** --
        ///       so the caller cannot assume "setting 0 means it definitely does not wait"; it is only a best-effort optimization.
        /// @note Idempotent: if the value equals the previous one, it is not sent again (saving one driver call per frame).
        void SetSwapInterval(int interval);

        /// @brief Whether the local driver supports controlling the swap interval (whether wglSwapIntervalEXT was obtained)
        bool canSetSwapInterval() const { return s_swapIntervalSupported; }

        /// @brief The swap interval currently sent (default 1)
        int swapInterval() const { return m_swapInterval; }

        // Queries
        HDC   GetHDC() const { return m_hDC; }
        HGLRC GetHGLRC() const { return m_hRC; }
        bool  IsValid() const { return m_hRC != nullptr; }

        /// @brief Actively releases the GL context / DC and returns the HWND to the window system.
        /// @note Must be called before switching backends (UseRenderer): GLContext::Create does SetPixelFormat +
        ///       wglMakeCurrent to "claim" the window for WGL; if not released, calling
        ///       vkCreateSwapchainKHR again on the same HWND reports VK_ERROR_NATIVE_WINDOW_IN_USE_KHR
        ///       (cross-API occupation). Internally this is Destroy, and the destructor runs it again (idempotent).
        void Release() { Destroy(); }

        // ===================================================================
        //  Shader helper functions
        // ===================================================================
        GLuint compileShader(GLenum type, const char* source);
        GLuint createProgram(const char* vertSrc, const char* fragSrc);

        // Gets the actual version reported by the current context (meaningful only after creation)
        void GetVersion(int& major, int& minor) const;

        /// Queries the **renderer actually in use** -- used on dual-GPU machines to confirm whether the discrete GPU is really selected.
        /// @note Requires this object to be the current context (satisfied after Create() returns and inside exec()).
        ///       When unavailable, each string is empty and isDiscrete = false.
        ///       The mechanism for forcing the discrete GPU is described in the top comment of ZeGLContext.cpp.
        RendererInfo GetRendererInfo() const;

    private:
        // Creates a temporary window + context solely to load WGL extension function pointers
        static bool LoadWGLExtensions();

        /// @brief Common body of `Create` / `CreateShared`
        /// @param shareHGLRC Sharing source; nullptr = create a standalone context
        bool CreateImpl(HWND hwnd, HGLRC shareHGLRC, int major, int minor, bool coreProfile);

        void Destroy();

        HWND  m_hwnd = nullptr;   // Not owned
        HDC   m_hDC = nullptr;   // Obtained from GetDC, ReleaseDC on destruction
        HGLRC m_hRC = nullptr;

        int   m_swapInterval = 1;   /// < Desired swap interval (semantic value)
        /// @brief Whether it was **actually** sent to the driver (see the notes on SetSwapInterval)
        bool  m_swapApplied = false;

        static bool s_wglLoaded;   // Only needs to be loaded once globally
        static bool s_swapIntervalSupported;   /// < Whether wglSwapIntervalEXT was obtained
	};
}