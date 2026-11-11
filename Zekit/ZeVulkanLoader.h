// =====================================================================
//  ZeVulkanLoader.h -- runtime loading of Vulkan (dlopen / LoadLibrary)
// ---------------------------------------------------------------------
//  ## Why not link-time dependency
//
//  Originally this was hard-linked via `-lvulkan` / `vulkan-1.lib`. Consequence: when the library
//  is missing on the machine, the process dies at load time
//  (`error while loading shared libraries: libvulkan.so.1`), and the Kit chain
//  "fall back to OpenGL if Vulkan cannot be created" never gets a chance to run --
//  the user sees the program fail to start instead of "automatically switched to GL".
//
//  On Linux this is especially fatal, because:
//    - OpenGL almost certainly exists (desktop environments always ship Mesa, even llvmpipe
//      software rasterization is there);
//    - Vulkan must be installed separately (loader + ICD), and `Rhi::DefaultApi()` happens to
//      default to Vulkan on Linux -- defaulting to the one least guaranteed to exist.
//  So "gracefully fall back to GL when Vulkan is missing" is not a nice-to-have, it is the
//  normal path on Linux.
//
//  ## Same-name variable trick (the volk approach)
//
//  `ZeVulkanContext.cpp` / `ZeVkUIRenderer.cpp` contain ordinary calls like
//  `vkCreateInstance(...)`. This header declares `vkXxx` as global function pointer
//  variables of the same name, so those two files need not change a single line;
//  the identifiers resolve to the variables automatically.
//
//  The cost is that `VK_NO_PROTOTYPES` must be uniform across the whole library: make `vulkan.h`
//  define only types (`PFN_vkXxx`) without declaring function prototypes -- otherwise whichever
//  header is included first brings in function declarations that directly conflict with the
//  variable declarations here.
//  WARNING: therefore the build script must add `-DVK_NO_PROTOTYPES` to all translation units
//     (see CXXFLAGS in build_zekit.sh / build_zekit_linux.sh).
//
//  ## Two phases
//
//      Vk::LoadLibrary()                // dlopen + global functions (before instance)
//      ... vkCreateInstance ...         // create instance
//      Vk::LoadInstanceFunctions(inst)  // fill in the rest (physical device / device level / WSI)
//
//  Only one symbol `vkGetInstanceProcAddr` needs dlsym; everything else is obtained from it --
//  this is the approach guaranteed by the Vulkan spec (it returns non-null for any valid name,
//  including itself).
// =====================================================================
#pragma once

// ---------------------------------------------------------------------
//  Platform macros must be set before the first include of <vulkan/vulkan.h>
// ---------------------------------------------------------------------
//  (this header may race ZeVulkanContext.h for "first", so it carries its own #ifndef guard;
//    the real guarantee comes from the -D in the build script, this is only a fallback.)
#ifndef VK_NO_PROTOTYPES
#  define VK_NO_PROTOTYPES 1
#endif

//  WARNING: `VK_USE_PLATFORM_*` is deliberately NOT defined here. Those two macros drag in
//    platform headers (Xlib's `None` / `Status` / `Bool`, or windows.h's min/max),
//    and this header is included by multiple translation units -- broadcasting that pollution is
//    exactly what `ZeVulkanContext.h` spends a whole section preventing (`ZeBoxStyle.h`'s
//    `RadiusMode::None` died on this).
//    The only cost: platform-specific surface creation functions are not declared here; the
//    single compilation unit that uses them fetches them itself (see
//    ZeVulkanContext::createSurface).
#include <vulkan/vulkan.h>

namespace Vk {

 /// @brief Loads the Vulkan loader (Linux: `libvulkan.so.1`; Windows: `vulkan-1.dll`)
 ///        and fills in the global functions (`vkCreateInstance` / `vkEnumerateInstanceLayerProperties`)
 /// @return false = no usable Vulkan loader on this machine
 /// @note Not an error. Callers should treat it as "Vulkan unavailable" and take the fallback.
 ///       The failure reason is in `LastError()` (plain text + install suggestion).
 /// @note Idempotent: repeated calls after success return true directly, no repeated dlopen.
	bool LoadLibrary();

 /// @brief Fills in instance / device level functions (physical device queries, command buffers, WSI...)
 /// @param instance `VkInstance` (passed as void* so this header does not drag vulkan.h into callers)
 /// @return false = a key entry point is missing; callers should treat it as "Vulkan unavailable"
 /// @note Must be called after `vkCreateInstance` succeeds -- before that these entry points
 ///       do not exist yet (the loader returns nullptr without an instance).
	bool LoadInstanceFunctions(void* instance);

 /// @brief Whether the library has loaded successfully (the sole basis for "can Vulkan be used")
	bool IsLoaded();

 /// @brief Reason for the last failure (plain text, with install suggestion); empty string if none
 /// @note The returned pointer is valid until the next `LoadLibrary()`; do not store it.
	const char* LastError();

} // namespace Vk

// =====================================================================
//  Global function pointers with the same names as the functions (83 cross-platform entry points)
// =====================================================================
//  Written as a list macro rather than 80 lines x 2 copies: declaration and definition share the
//  same list, so adding a function later touches only this one place and avoids the silly
//  "declared but not defined" error.
#define ZEKIT_VK_FUNCTION_LIST(X) \
	X(vkAcquireNextImageKHR)                     \
	X(vkAllocateCommandBuffers)                  \
	X(vkAllocateDescriptorSets)                  \
	X(vkAllocateMemory)                          \
	X(vkBeginCommandBuffer)                      \
	X(vkBindBufferMemory)                        \
	X(vkBindImageMemory)                         \
	X(vkCmdBeginRenderPass)                      \
	X(vkCmdBindDescriptorSets)                   \
	X(vkCmdBindIndexBuffer)                      \
	X(vkCmdBindPipeline)                         \
	X(vkCmdBindVertexBuffers)                    \
	X(vkCmdCopyBufferToImage)                    \
	X(vkCmdDrawIndexed)                          \
	X(vkCmdEndRenderPass)                        \
	X(vkCmdPipelineBarrier)                      \
	X(vkCmdPushConstants)                        \
	X(vkCmdSetScissor)                           \
	X(vkCmdSetViewport)                          \
	X(vkCreateBuffer)                            \
	X(vkCreateCommandPool)                       \
	X(vkCreateDescriptorPool)                    \
	X(vkCreateDescriptorSetLayout)               \
	X(vkCreateDevice)                            \
	X(vkCreateFence)                             \
	X(vkCreateFramebuffer)                       \
	X(vkCreateGraphicsPipelines)                 \
	X(vkCreateImage)                             \
	X(vkCreateImageView)                         \
	X(vkCreateInstance)                          \
	X(vkCreatePipelineLayout)                    \
	X(vkCreateRenderPass)                        \
	X(vkCreateSampler)                           \
	X(vkCreateSemaphore)                         \
	X(vkCreateShaderModule)                      \
	X(vkCreateSwapchainKHR)                      \
	X(vkDestroyBuffer)                           \
	X(vkDestroyCommandPool)                      \
	X(vkDestroyDescriptorPool)                   \
	X(vkDestroyDescriptorSetLayout)              \
	X(vkDestroyDevice)                           \
	X(vkDestroyFence)                            \
	X(vkDestroyFramebuffer)                      \
	X(vkDestroyImage)                            \
	X(vkDestroyImageView)                        \
	X(vkDestroyInstance)                         \
	X(vkDestroyPipeline)                         \
	X(vkDestroyPipelineLayout)                   \
	X(vkDestroyRenderPass)                       \
	X(vkDestroySampler)                          \
	X(vkDestroySemaphore)                        \
	X(vkDestroyShaderModule)                     \
	X(vkDestroySurfaceKHR)                       \
	X(vkDestroySwapchainKHR)                     \
	X(vkDeviceWaitIdle)                          \
	X(vkEndCommandBuffer)                        \
	X(vkEnumerateDeviceExtensionProperties)      \
	X(vkEnumerateInstanceLayerProperties)        \
	X(vkEnumeratePhysicalDevices)                \
	X(vkFreeCommandBuffers)                      \
	X(vkFreeDescriptorSets)                      \
	X(vkFreeMemory)                              \
	X(vkGetBufferMemoryRequirements)             \
	X(vkGetDeviceQueue)                          \
	X(vkGetImageMemoryRequirements)              \
	X(vkGetInstanceProcAddr)                     \
	X(vkGetPhysicalDeviceMemoryProperties)       \
	X(vkGetPhysicalDeviceProperties)             \
	X(vkGetPhysicalDeviceProperties2)            \
	X(vkGetPhysicalDeviceQueueFamilyProperties)  \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR)      \
	X(vkGetSwapchainImagesKHR)                   \
	X(vkMapMemory)                               \
	X(vkQueuePresentKHR)                         \
	X(vkQueueSubmit)                             \
	X(vkResetCommandBuffer)                      \
	X(vkResetFences)                             \
	X(vkUnmapMemory)                             \
	X(vkUpdateDescriptorSets)                    \
	X(vkWaitForFences)

#define ZEKIT_VK_DECLARE(name) extern PFN_##name name;
ZEKIT_VK_FUNCTION_LIST(ZEKIT_VK_DECLARE)
#undef ZEKIT_VK_DECLARE
