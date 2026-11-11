#include "pch.h"
#include "ZeVulkanLoader.h"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>

#define ZEKIT_VK_DEFINE(name) PFN_##name name = nullptr;
ZEKIT_VK_FUNCTION_LIST(ZEKIT_VK_DEFINE)
#undef ZEKIT_VK_DEFINE

namespace {

	void*       g_lib = nullptr;
	bool        g_loaded = false;
	std::string g_error;

	const char* LibraryName() {
		if (const char* v = std::getenv("ZEKIT_VULKAN_LIB"))
			if (*v) return v;
#if defined(_WIN32)
		return "vulkan-1.dll";
#elif defined(__ANDROID__)
		return "libvulkan.so";
#elif defined(__OHOS__)
		return "libvulkan.so";
#elif defined(__linux__)
		return "libvulkan.so.1";
#else
		return nullptr;
#endif
	}

	void* OpenLibrary(const char* name) {
#if defined(_WIN32)
		return reinterpret_cast<void*>(::LoadLibraryA(name));
#else
		return ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
	}

	void* FindSymbol(void* lib, const char* name) {
#if defined(_WIN32)
		return reinterpret_cast<void*>(
			::GetProcAddress(static_cast<HMODULE>(lib), name));
#else
		return ::dlsym(lib, name);
#endif
	}

	void FillFromLoader(VkInstance inst) {
		if (!::vkGetInstanceProcAddr) return;

#define ZEKIT_VK_FILL(name)                                                   \
		if (!name)                                                            \
			name = reinterpret_cast<PFN_##name>(                              \
				::vkGetInstanceProcAddr(inst, #name));
		ZEKIT_VK_FUNCTION_LIST(ZEKIT_VK_FILL)
#undef ZEKIT_VK_FILL
	}

	bool TraceOn() {
		if (const char* v = std::getenv("ZEKIT_TRACE_LIFECYCLE"))
			return *v && *v != '0';
		return false;
	}

}

namespace Vk {

	bool LoadLibrary() {
		if (g_loaded) return true;

		const char* lib = LibraryName();
		if (!lib) {
			g_error = "no Vulkan loader name known for this platform";
			return false;
		}

		if (!g_lib) {
			g_lib = OpenLibrary(lib);
			if (!g_lib) {
				g_error = std::string("Vulkan loader '") + lib + "' not found. "
#if defined(_WIN32)
					"Install or update your GPU driver (it ships vulkan-1.dll).";
#elif defined(__ANDROID__)
					"This device has no Vulkan loader (libvulkan.so) — "
					"the window will fall back to OpenGL ES.";
#elif defined(__OHOS__)
					"This HarmonyOS device has no Vulkan loader (libvulkan.so) — "
					"the window will fall back to OpenGL ES (EGL/GLES3).";
#else
					"Install it with e.g. 'sudo apt install libvulkan1' "
					"(Fedora: 'sudo dnf install vulkan-loader'); "
					"Vulkan stays unavailable without it and the window falls back to OpenGL.";
#endif
				return false;
			}
			if (TraceOn())
				std::fprintf(stderr, "[Zekit/trace] Vulkan loader 已加载：%s\n", lib);
		}

		::vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
			FindSymbol(g_lib, "vkGetInstanceProcAddr"));
		if (!::vkGetInstanceProcAddr) {
			g_error = std::string("'") + lib +
				"' does not export vkGetInstanceProcAddr (不是 Vulkan loader？)";
			return false;
		}

		FillFromLoader(VK_NULL_HANDLE);
		if (!::vkCreateInstance || !::vkEnumerateInstanceLayerProperties) {
			g_error = std::string("'") + lib +
				"' is missing required entry points "
				"(vkCreateInstance / vkEnumerateInstanceLayerProperties)";
			return false;
		}

		g_loaded = true;
		g_error.clear();
		return true;
	}

	bool LoadInstanceFunctions(void* instance) {
		if (!g_loaded || !instance) return false;

		FillFromLoader(reinterpret_cast<VkInstance>(instance));

		if (!::vkCreateDevice || !::vkGetDeviceQueue ||
			!::vkQueueSubmit || !::vkQueuePresentKHR ||
			!::vkCreateSwapchainKHR) {
			g_error = "Vulkan instance is missing device-level entry points "
				"(loader/instance version mismatch?)";
			return false;
		}
		return true;
	}

	bool IsLoaded() { return g_loaded; }

	const char* LastError() { return g_error.c_str(); }

}
