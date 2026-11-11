#include "pch.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
#  ifndef VK_USE_PLATFORM_XLIB_KHR
#    define VK_USE_PLATFORM_XLIB_KHR 1
#  endif
#endif

#if defined(__ANDROID__)
#  ifndef VK_USE_PLATFORM_ANDROID_KHR
#    define VK_USE_PLATFORM_ANDROID_KHR 1
#  endif
#endif

#  if defined(__OHOS__)
#    ifndef VK_USE_PLATFORM_OHOS
#      define VK_USE_PLATFORM_OHOS 1
#    endif
#  endif

#include "ZeVulkanContext.h"
#include "ZeVulkanLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
#  include <dlfcn.h>
#  include <X11/Xlib.h>
#  undef None
#  undef Status
#  undef Bool
#  undef Success
#  undef True
#  undef False
#  undef Complex
#  undef Convex
#endif

namespace Vk {
    namespace {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
        void PinVulkanDriverLibraries() {
            FILE* f = std::fopen("/proc/self/maps", "r");
            if (!f) return;
            std::set<std::string> pinned;
            char line[4096];
            while (std::fgets(line, sizeof line, f)) {
                const char* slash = std::strrchr(line, '/');
                if (!slash) continue;
                const char* base = slash + 1;
                const bool isIcd =
                    std::strncmp(base, "libvulkan_", 10) == 0 ||
                    std::strncmp(base, "libGLX_", 7) == 0;
                if (!isIcd) continue;
                std::string path(base);
                while (!path.empty() &&
                       (path.back() == '\n' || path.back() == '\r' ||
                        path.back() == ' '))
                    path.pop_back();
                if (path.empty() || !pinned.insert(path).second) continue;
                (void)::dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
            }
            std::fclose(f);
        }
#endif

        inline const char* ResultName(VkResult r)
        {
            switch (r) {
                case VK_ERROR_OUT_OF_HOST_MEMORY:       return "OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED:    return "INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST:              return "DEVICE_LOST";
                case VK_ERROR_LAYER_NOT_PRESENT:        return "LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT:    return "EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT:      return "FEATURE_NOT_PRESENT";
                case VK_ERROR_TOO_MANY_OBJECTS:         return "TOO_MANY_OBJECTS";
                case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "FORMAT_NOT_SUPPORTED";
                case VK_ERROR_SURFACE_LOST_KHR:
                    return "SURFACE_LOST_KHR (surface lost: the window was destroyed, "
                           "or the display mode changed)";
                case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
                    return "NATIVE_WINDOW_IN_USE_KHR (native window already in use: "
                           "another swapchain already exists on this window, or the window "
                           "is held exclusively by another graphics API / frame capture tool)";
                case VK_ERROR_OUT_OF_DATE_KHR:
                    return "OUT_OF_DATE_KHR (swapchain no longer matches the surface, "
                           "usually because the window was resized)";
                default:                                return "unknown error code";
            }
        }

        inline bool ValidationLayerWanted(bool buildDefault)
        {
            const char* v = std::getenv("ZEKIT_VULKAN_VALIDATION");
            if (!v || *v == '\0') return buildDefault;
            if (std::strcmp(v, "0") == 0 || std::strcmp(v, "off") == 0 ||
                std::strcmp(v, "false") == 0 || std::strcmp(v, "no") == 0)
                return false;
            return true;
        }

        inline bool TraceStartup()
        {
            static const bool on = [] {
                const char* v = std::getenv("ZEKIT_TRACE_LIFECYCLE");
                return v && *v != '\0' && *v != '0';
                }();
            return on;
        }

        inline long long MsSince(std::chrono::steady_clock::time_point t0)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
        }

        template <size_t N>
        inline void CopyTruncated(char (&dst)[N], const char* src) {
            if (N == 0) return;
            std::snprintf(dst, N, "%.*s", static_cast<int>(N - 1), src ? src : "");
        }

#define VK_CHECK(x, msg)                                                    \
    do {                                                                    \
        VkResult res = (x);                                                 \
        if (res != VK_SUCCESS) {                                            \
            throw std::runtime_error(std::string("Vulkan error [") + msg +  \
                                     "]: code " + std::to_string(res) +     \
                                     " (" + ResultName(res) + ")");         \
        }                                                                   \
    } while (0)

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT      ,
            VkDebugUtilsMessageTypeFlagsEXT             ,
            const VkDebugUtilsMessengerCallbackDataEXT* data,
            void*                                       ) {
            std::cerr << "[Vulkan] " << data->pMessage << std::endl;
            return VK_FALSE;
        }

        static VkResult CreateDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
            const VkAllocationCallbacks* pAllocator,
            VkDebugUtilsMessengerEXT* pMessenger) {
            auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (func) return func(instance, pCreateInfo, pAllocator, pMessenger);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        static void DestroyDebugUtilsMessengerEXT(
            VkInstance instance,
            VkDebugUtilsMessengerEXT messenger,
            const VkAllocationCallbacks* pAllocator) {
            auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (func) func(instance, messenger, pAllocator);
        }
    }

    VulkanContext::~VulkanContext() { cleanup(); }

    bool VulkanContext::initDevice(const NativeWindow& native, Rhi::Gpu gpu) {
        if (m_device != VK_NULL_HANDLE) return true;

        if (!Vk::LoadLibrary())
            throw std::runtime_error(Vk::LastError());

        m_native = native;
        m_gpu = gpu;
        m_deviceInfo = {};

        const bool trace = TraceStartup();
        const auto   t0 = std::chrono::steady_clock::now();

        try {
            createInstance();
            setupDebugMessenger();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
        } catch (...) {
            cleanup();
            throw;
        }

        if (trace)
            std::cerr << "[Zekit/trace] ② 设备段就绪（驱动加载/实例/设备） " << MsSince(t0) << "ms\n";
        return true;
    }

    void VulkanContext::initSwapchain() {
        if (m_swapchain != VK_NULL_HANDLE) return;

        const bool trace = TraceStartup();
        const auto   t0 = std::chrono::steady_clock::now();

        try {
            createSwapchain();
            createRenderPass();
            createImageViews();
            createFramebuffers();
            createRenderFinishedSemaphores();
            createCommandPool();
            createCommandBuffers();
            createSyncObjects();
        } catch (...) {
            cleanup();
            throw;
        }

        if (trace)
            std::cerr << "[Zekit/trace] ④ 交换链就绪 " << MsSince(t0) << "ms\n";
    }

    bool VulkanContext::preheat(const NativeWindow& native, Rhi::Gpu gpu) {
        if (m_device != VK_NULL_HANDLE) return true;
        try {
            initDevice(native, gpu);
            return true;
        } catch (const std::exception& e) {
            cleanup();
            if (TraceStartup())
                std::cerr << "[Zekit/trace] ② 预热失败（不影响正确性，稍后走完整路径）: "
                          << e.what() << "\n";
            return false;
        }
    }

    void VulkanContext::init(const NativeWindow& native, Rhi::Gpu gpu) {
        initDevice(native, gpu);
        initSwapchain();
    }

    bool VulkanContext::setClientSize(int width, int height) {
        const uint32_t w = width  > 0 ? static_cast<uint32_t>(width)  : 0u;
        const uint32_t h = height > 0 ? static_cast<uint32_t>(height) : 0u;
        const bool changed = (w != m_clientW) || (h != m_clientH);
        m_clientW = w;
        m_clientH = h;
        return changed;
    }

    bool VulkanContext::clientSize(uint32_t& w, uint32_t& h) const {
#if defined(_WIN32)
        RECT rc{};
        if (GetClientRect(reinterpret_cast<HWND>(static_cast<uintptr_t>(m_native.window)), &rc)) {
            const long cw = rc.right - rc.left;
            const long ch = rc.bottom - rc.top;
            if (cw > 0 && ch > 0) {
                w = static_cast<uint32_t>(cw);
                h = static_cast<uint32_t>(ch);
                return true;
            }
        }
        return false;
#else
        if (m_clientW == 0 || m_clientH == 0) return false;
        w = m_clientW;
        h = m_clientH;
        return true;
#endif
    }

    Rhi::DeviceInfo VulkanContext::deviceInfo() const {
        if (m_physicalDevice == VK_NULL_HANDLE) return {};
        return m_deviceInfo;
    }

    void VulkanContext::waitIdle() {
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
    }

    void VulkanContext::cleanup() {
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);

            m_recordFn = nullptr;

            for (VkSemaphore s : m_imageAvailable) if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (VkFence     f : m_inFlight)       if (f) vkDestroyFence(m_device, f, nullptr);
            m_imageAvailable.clear();
            m_inFlight.clear();

            cleanupSwapchain();

            for (RetiredSwapchain& r : m_retired) {
                destroyFramebuffers(r.framebuffers);
                for (VkSemaphore s : r.renderFinished)
                    if (s) vkDestroySemaphore(m_device, s, nullptr);
                for (VkImageView v : r.views)
                    if (v) vkDestroyImageView(m_device, v, nullptr);
                if (r.swapchain)
                    vkDestroySwapchainKHR(m_device, r.swapchain, nullptr);
            }
            m_retired.clear();

            if (m_renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(m_device, m_renderPass, nullptr);
                m_renderPass = VK_NULL_HANDLE;
                m_renderPassFormat = VK_FORMAT_UNDEFINED;
            }

            if (m_commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                m_commandPool = VK_NULL_HANDLE;
            }

            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }

        if (m_debugMessenger != VK_NULL_HANDLE) {
            DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }

        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_instance != VK_NULL_HANDLE) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
            PinVulkanDriverLibraries();
#endif
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::createInstance() {
        if (m_instance != VK_NULL_HANDLE) return;

        const bool wantValidation = ValidationLayerWanted(kWantValidationLayers);
        m_useValidationLayers = wantValidation && checkValidationLayerSupport();

        static bool s_validationNoticePrinted = false;
        if (wantValidation && !m_useValidationLayers && !s_validationNoticePrinted) {
            s_validationNoticePrinted = true;
            std::cerr << "[Zekit] validation layer '"
                << (m_validationLayers.empty() ? "?" : m_validationLayers.front())
                << "' requested but not installed; continuing without it.\n"
                   "        (install: sudo apt install vulkan-validationlayers)\n";
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Zekit";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Zekit";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        auto extensions = getRequiredExtensions();

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (m_useValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
            createInfo.ppEnabledLayerNames = m_validationLayers.data();

            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = debugCallback;

            createInfo.pNext = &debugCreateInfo;
        }

        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance),
            "vkCreateInstance");

        if (!Vk::LoadInstanceFunctions(reinterpret_cast<void*>(m_instance)))
            throw std::runtime_error(Vk::LastError());
    }

    void VulkanContext::setupDebugMessenger() {
        if (!m_useValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;

        VK_CHECK(CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger),
            "CreateDebugUtilsMessengerEXT");
    }

    void VulkanContext::createSurface() {
#if defined(_WIN32)
        VkWin32SurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        ci.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(m_native.window));
        ci.hinstance = static_cast<HINSTANCE>(m_native.instance);

        const auto pfnCreateSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            vkGetInstanceProcAddr(m_instance, "vkCreateWin32SurfaceKHR"));
        if (!pfnCreateSurface)
            throw std::runtime_error("vkCreateWin32SurfaceKHR not available");

        VK_CHECK(pfnCreateSurface(m_instance, &ci, nullptr, &m_surface),
            "vkCreateWin32SurfaceKHR");
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
        if (!m_native.display || m_native.window == 0) {
            throw std::runtime_error(
                "Vulkan surface: missing X11 display or window id "
                "(Plat::X11DisplayHandle()/X11WindowId() returned nothing)");
        }

        VkXlibSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        ci.dpy = static_cast<Display*>(m_native.display);
        ci.window = static_cast<::Window>(m_native.window);

        const auto pfnCreateSurface = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
            vkGetInstanceProcAddr(m_instance, "vkCreateXlibSurfaceKHR"));
        if (!pfnCreateSurface)
            throw std::runtime_error("vkCreateXlibSurfaceKHR not available");

        VK_CHECK(pfnCreateSurface(m_instance, &ci, nullptr, &m_surface),
            "vkCreateXlibSurfaceKHR");
#elif defined(__ANDROID__)
        if (m_native.window == 0) {
            throw std::runtime_error(
                "Vulkan surface: missing ANativeWindow "
                "(Plat 层还没拿到 android_app::window —— 通常是 APP_CMD_INIT_WINDOW 还没到；"
                "窗口在切后台 / 旋转屏幕时会先 TERM 再 INIT，这段时间它会是空的)");
        }

        VkAndroidSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        ci.pNext = nullptr;
        ci.flags = 0;
        ci.window = reinterpret_cast<ANativeWindow*>(
            static_cast<uintptr_t>(m_native.window));

        const auto pfnCreateSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
            vkGetInstanceProcAddr(m_instance, "vkCreateAndroidSurfaceKHR"));
        if (!pfnCreateSurface)
            throw std::runtime_error("vkCreateAndroidSurfaceKHR not available");

        VK_CHECK(pfnCreateSurface(m_instance, &ci, nullptr, &m_surface),
            "vkCreateAndroidSurfaceKHR");
#elif defined(__OHOS__)
        if (m_native.window == 0) {
            throw std::runtime_error(
                "Vulkan surface: missing OHNativeWindow "
                "(Plat 层还没拿到 XComponent 的 surface —— 通常是 "
                "OnSurfaceCreated 还没到；surface 在切后台 / 旋转 / 尺寸变化时会 "
                "先 Destroyed 再 Created，这段时间它会是空的)");
        }

        VkSurfaceCreateInfoOHOS ci{};
        ci.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
        ci.pNext = nullptr;
        ci.flags = 0;
        ci.window = reinterpret_cast<OHNativeWindow*>(
            static_cast<uintptr_t>(m_native.window));

        const auto pfnCreateSurface = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
            vkGetInstanceProcAddr(m_instance, "vkCreateSurfaceOHOS"));
        if (!pfnCreateSurface)
            throw std::runtime_error("vkCreateSurfaceOHOS not available");

        VK_CHECK(pfnCreateSurface(m_instance, &ci, nullptr, &m_surface),
            "vkCreateSurfaceOHOS");
#else
        throw std::runtime_error("No Vulkan surface backend for this platform");
#endif
    }

    void VulkanContext::pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found!");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

        m_physicalDevice = choosePhysicalDevice(devices, m_gpu);
        if (m_physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error(
                std::string("no suitable GPU for preference '") + Rhi::GpuName(m_gpu) +
                "' (strict preferences fail instead of falling back silently)");
        }

        VkPhysicalDeviceDriverProperties drv{};
        drv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &drv;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

        const VkPhysicalDeviceProperties& p = props2.properties;

        CopyTruncated(m_deviceInfo.name, p.deviceName);
        CopyTruncated(m_deviceInfo.driver,
            drv.driverInfo[0] ? drv.driverInfo : drv.driverName);
        CopyTruncated(m_deviceInfo.api, "Vulkan");
        m_deviceInfo.isDiscrete = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        m_deviceInfo.valid = true;
    }

    VkPhysicalDevice VulkanContext::choosePhysicalDevice(
        const std::vector<VkPhysicalDevice>& devices, Rhi::Gpu gpu) const {

        static const VkPhysicalDeviceType kDiscreteFirst[] = {
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
            VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
            VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
            VK_PHYSICAL_DEVICE_TYPE_CPU,
        };
        static const VkPhysicalDeviceType kIntegratedFirst[] = {
            VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
            VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
            VK_PHYSICAL_DEVICE_TYPE_CPU,
        };
        static const VkPhysicalDeviceType kDiscreteOnly[] = {
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
        };

        const VkPhysicalDeviceType* order = kDiscreteFirst;
        size_t orderCount = sizeof(kDiscreteFirst) / sizeof(kDiscreteFirst[0]);

        switch (gpu) {
        case Rhi::Gpu::Discrete:
            order = kDiscreteOnly;
            orderCount = sizeof(kDiscreteOnly) / sizeof(kDiscreteOnly[0]);
            break;
        case Rhi::Gpu::Integrated:
            order = kIntegratedFirst;
            orderCount = sizeof(kIntegratedFirst) / sizeof(kIntegratedFirst[0]);
            break;
        case Rhi::Gpu::Auto:
        default:
            break;
        }

        for (size_t t = 0; t < orderCount; ++t) {
            for (auto dev : devices) {
                VkPhysicalDeviceProperties p{};
                vkGetPhysicalDeviceProperties(dev, &p);
                if (p.deviceType != order[t]) continue;
                if (!isDeviceSuitable(dev)) continue;
                return dev;
            }
        }
        return VK_NULL_HANDLE;
    }

    bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) const {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

        bool hasGraphics = false;
        bool hasPresent = false;

        for (uint32_t i = 0; i < queueCount; ++i) {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                hasGraphics = true;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
            if (presentSupport) hasPresent = true;

            if (hasGraphics && hasPresent) break;
        }
        if (!hasGraphics || !hasPresent) return false;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

        std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
        for (auto& e : exts) required.erase(e.extensionName);

        return required.empty();
    }

    void VulkanContext::createLogicalDevice() {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, queues.data());

        bool foundGraphics = false;
        bool foundPresent = false;

        for (uint32_t i = 0; i < queueCount; ++i) {
            if (!foundGraphics && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                m_graphicsQueueFamily = i;
                foundGraphics = true;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
            if (!foundPresent && presentSupport) {
                m_presentQueueFamily = i;
                foundPresent = true;
            }
            if (foundGraphics && foundPresent) break;
        }

        std::set<uint32_t> uniqueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily };
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        float priority = 1.0f;
        for (uint32_t fam : uniqueFamilies) {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = fam;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queueInfos.push_back(qi);
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = nullptr;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

        VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device),
            "vkCreateDevice");

        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
    }

    VkSurfaceFormatKHR VulkanContext::chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const {

        if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
            return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        }

        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
        }
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) return f;
        }
        return formats[0];
    }

    VkPresentModeKHR VulkanContext::choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const {
        for (VkPresentModeKHR m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) return VK_PRESENT_MODE_MAILBOX_KHR;
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanContext::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
        if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;

        uint32_t w = 0, h = 0;
        if (!clientSize(w, h)) {
            w = caps.minImageExtent.width;
            h = caps.minImageExtent.height;
        }
        VkExtent2D actual{ w, h };
        actual.width = std::max(caps.minImageExtent.width,
            std::min(caps.maxImageExtent.width, actual.width));
        actual.height = std::max(caps.minImageExtent.height,
            std::min(caps.maxImageExtent.height, actual.height));
        return actual;
    }

    void VulkanContext::createSwapchain(VkSwapchainKHR oldSwapchain) {
        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (formatCount) vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        if (modeCount) vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());

        const VkSurfaceFormatKHR fmt = chooseSurfaceFormat(formats);
        const VkPresentModeKHR   pm = choosePresentMode(modes);
        if (TraceStartup() && pm != m_presentMode) {
            std::cerr << "[Vulkan] swapchain present mode → "
                << (pm == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX（不阻塞 CPU，推荐）"
                    : pm == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE（无同步）"
                    : pm == VK_PRESENT_MODE_FIFO_KHR ? "FIFO（垂直同步，会阻塞）"
                    : "其他") << "\n";
        }
        m_presentMode = pm;
        const VkExtent2D         ext = chooseExtent(caps);

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
            imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = m_surface;
        ci.minImageCount = imageCount;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        {
            VkCompositeAlphaFlagBitsKHR ca = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            if (m_clearAlpha <= 0.0f) {
                if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
                    ca = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
                else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
                    ca = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
            }
            ci.compositeAlpha = ca;
            m_perPixelTransparent = (ca != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
        }
        ci.presentMode = pm;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = oldSwapchain;

        const uint32_t famIndices[2] = { m_graphicsQueueFamily, m_presentQueueFamily };
        if (m_graphicsQueueFamily != m_presentQueueFamily) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = famIndices;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VkResult swapRes = vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain);
        if (swapRes != VK_SUCCESS) {
            reportSurfaceDiagnostics();
        }
        VK_CHECK(swapRes, "vkCreateSwapchainKHR");

        uint32_t actual = 0;
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, nullptr);
        m_swapImages.resize(actual);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual, m_swapImages.data());

        m_swapFormat = fmt.format;
        m_swapExtent = ext;

    }

    void VulkanContext::createImageViews() {
        m_swapImageViews.resize(m_swapImages.size());

        for (size_t i = 0; i < m_swapImages.size(); ++i) {
            VkImageViewCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image = m_swapImages[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = m_swapFormat;
            ci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.baseMipLevel = 0;
            ci.subresourceRange.levelCount = 1;
            ci.subresourceRange.baseArrayLayer = 0;
            ci.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(m_device, &ci, nullptr, &m_swapImageViews[i]),
                "vkCreateImageView");
        }
    }

    void VulkanContext::createRenderPass() {
        if (m_renderPass != VK_NULL_HANDLE && m_renderPassFormat == m_swapFormat) return;
        if (m_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }

        VkAttachmentDescription color{};
        color.format = m_swapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;

        VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass),
            "vkCreateRenderPass");
        m_renderPassFormat = m_swapFormat;
    }

    void VulkanContext::createFramebuffers() {
        m_swapFramebuffers.assign(m_swapImageViews.size(), VK_NULL_HANDLE);

        for (size_t i = 0; i < m_swapImageViews.size(); ++i) {
            VkImageView attachments[] = { m_swapImageViews[i] };

            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = m_renderPass;
            ci.attachmentCount = 1;
            ci.pAttachments = attachments;
            ci.width = m_swapExtent.width;
            ci.height = m_swapExtent.height;
            ci.layers = 1;

            VK_CHECK(vkCreateFramebuffer(m_device, &ci, nullptr, &m_swapFramebuffers[i]),
                "vkCreateFramebuffer");
        }
    }

    void VulkanContext::destroyFramebuffers(std::vector<VkFramebuffer>& fbs) {
        for (VkFramebuffer f : fbs) if (f) vkDestroyFramebuffer(m_device, f, nullptr);
        fbs.clear();
    }

    void VulkanContext::createSyncObjects() {
        m_imageAvailable.assign(kFramesInFlight, VK_NULL_HANDLE);
        m_inFlight.assign(kFramesInFlight, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            VK_CHECK(vkCreateSemaphore(m_device, &si, nullptr, &m_imageAvailable[i]),
                "vkCreateSemaphore(imageAvailable)");
            VK_CHECK(vkCreateFence(m_device, &fi, nullptr, &m_inFlight[i]),
                "vkCreateFence");
        }
    }

    void VulkanContext::createRenderFinishedSemaphores() {
        m_renderFinished.assign(m_swapImages.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (size_t i = 0; i < m_renderFinished.size(); ++i) {
            VK_CHECK(vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinished[i]),
                "vkCreateSemaphore(renderFinished)");
        }
    }

    void VulkanContext::createCommandPool() {
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = m_graphicsQueueFamily;

        VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool),
            "vkCreateCommandPool");
    }

    void VulkanContext::createCommandBuffers() {
        m_commandBuffers.resize(kFramesInFlight);

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, m_commandBuffers.data()),
            "vkAllocateCommandBuffers");
    }

    void VulkanContext::recordClear(VkCommandBuffer cmd, uint32_t imageIndex,
                                    float r, float g, float b) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_renderPass;
        rp.framebuffer = m_swapFramebuffers[imageIndex];
        rp.renderArea.offset = { 0, 0 };
        rp.renderArea.extent = m_swapExtent;

        VkClearValue clear{};
        if (m_clearAlpha <= 0.0f)
            clear.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        else
            clear.color = { { r, g, b, m_clearAlpha } };
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        if (m_recordFn) {
            FrameInfo fi;
            fi.cmd = cmd;
            fi.imageIndex = imageIndex;
            fi.frameSlot = m_frameIndex;
            fi.extent = m_swapExtent;
            fi.uiExtent = (m_uiW > 0 && m_uiH > 0)
                ? VkExtent2D{ m_uiW, m_uiH }
                : m_swapExtent;
            fi.format = m_swapFormat;
            m_recordFn(fi);
        }

        vkCmdEndRenderPass(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
    }

    bool VulkanContext::drawFrame(float r, float g, float b) {
        if (!isReady()) return false;

        try {
            uint32_t cw = 0, ch = 0;
            if (!clientSize(cw, ch)) return false;

            if (cw != m_swapExtent.width || ch != m_swapExtent.height) {
                if (!recreateSwapchain()) return false;
            }

            vkWaitForFences(m_device, 1, &m_inFlight[m_frameIndex], VK_TRUE, UINT64_MAX);

            collectRetiredSwapchains();

            uint32_t imageIndex = 0;
            const VkResult acquired = vkAcquireNextImageKHR(
                m_device, m_swapchain, UINT64_MAX,
                m_imageAvailable[m_frameIndex], VK_NULL_HANDLE, &imageIndex);

            if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                recreateSwapchain();
                return false;
            }
            if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) return false;

            vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);

            vkResetCommandBuffer(m_commandBuffers[m_frameIndex], 0);
            recordClear(m_commandBuffers[m_frameIndex], imageIndex, r, g, b);

            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            const VkSemaphore     waitSems[1] = { m_imageAvailable[m_frameIndex] };
            const VkPipelineStageFlags waitStages[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            const VkSemaphore     signalSems[1] = { m_renderFinished[imageIndex] };

            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = waitSems;
            si.pWaitDstStageMask = waitStages;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &m_commandBuffers[m_frameIndex];
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = signalSems;

            VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, m_inFlight[m_frameIndex]),
                "vkQueueSubmit");

            VkPresentInfoKHR pi{};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = signalSems;
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_swapchain;
            pi.pImageIndices = &imageIndex;

            const VkResult presented = vkQueuePresentKHR(m_presentQueue, &pi);
            if (presented == VK_ERROR_OUT_OF_DATE_KHR) {
                recreateSwapchain();
                return false;
            }

            m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "[Vulkan] drawFrame failed: " << e.what() << std::endl;
            return false;
        }
    }

    void VulkanContext::cleanupSwapchain() {
        destroyFramebuffers(m_swapFramebuffers);

        for (VkSemaphore s : m_renderFinished) {
            if (s) vkDestroySemaphore(m_device, s, nullptr);
        }
        m_renderFinished.clear();

        for (VkImageView v : m_swapImageViews) {
            if (v) vkDestroyImageView(m_device, v, nullptr);
        }
        m_swapImageViews.clear();
        m_swapImages.clear();

        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::retireCurrentSwapchain() {
        if (m_swapchain == VK_NULL_HANDLE) return;

        RetiredSwapchain r;
        r.swapchain = m_swapchain;
        r.views = std::move(m_swapImageViews);
        r.framebuffers = std::move(m_swapFramebuffers);
        r.renderFinished = std::move(m_renderFinished);
        r.framesToWait = kFramesInFlight + 1;

        m_retired.push_back(std::move(r));

        m_swapImageViews.clear();
        m_swapFramebuffers.clear();
        m_swapImages.clear();
        m_renderFinished.clear();
        m_swapchain = VK_NULL_HANDLE;
    }

    void VulkanContext::collectRetiredSwapchains() {
        for (auto it = m_retired.begin(); it != m_retired.end(); ) {
            if (it->framesToWait > 0) {
                --it->framesToWait;
                ++it;
                continue;
            }
            destroyFramebuffers(it->framebuffers);
            for (VkSemaphore s : it->renderFinished)
                if (s) vkDestroySemaphore(m_device, s, nullptr);
            for (VkImageView v : it->views)
                if (v) vkDestroyImageView(m_device, v, nullptr);
            if (it->swapchain)
                vkDestroySwapchainKHR(m_device, it->swapchain, nullptr);
            it = m_retired.erase(it);
        }
    }

    bool VulkanContext::recreateSwapchain() {
        uint32_t cw = 0, ch = 0;
        if (!clientSize(cw, ch))
            return false;

        VkSwapchainKHR oldSwapchain = m_swapchain;
        retireCurrentSwapchain();

        createSwapchain(oldSwapchain);
        createRenderPass();
        createImageViews();
        createFramebuffers();
        createRenderFinishedSemaphores();
        return true;
    }

    bool VulkanContext::checkValidationLayerSupport() const {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());

        for (const char* name : m_validationLayers) {
            bool found = false;
            for (auto& l : layers) {
                if (strcmp(l.layerName, name) == 0) { found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    }

    std::vector<const char*> VulkanContext::getRequiredExtensions() const {
        std::vector<const char*> exts = {
            "VK_KHR_surface",
#if defined(_WIN32)
            "VK_KHR_win32_surface",
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
            "VK_KHR_xlib_surface",
#elif defined(__ANDROID__)
            "VK_KHR_android_surface",
#elif defined(__OHOS__)
            "VK_OHOS_surface",
#endif
        };
        if (m_useValidationLayers) {
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return exts;
    }

    void VulkanContext::reportSurfaceDiagnostics() const {
        std::cerr << "[Vulkan] swapchain creation failed, window state: ";
#if defined(_WIN32)
        HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(m_native.window));
        const bool visible = IsWindowVisible(hwnd) != FALSE;
        RECT cr{}; GetClientRect(hwnd, &cr);
        std::cerr << "visible=" << visible
            << " client=" << (cr.right - cr.left) << "x" << (cr.bottom - cr.top)
            << " hasParent=" << (GetParent(hwnd) != nullptr)
            << " hwnd=" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec;
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)
        std::cerr << "x11 display=" << m_native.display
            << " xid=0x" << std::hex << m_native.window << std::dec
            << " client=" << m_clientW << "x" << m_clientH;
#elif defined(__ANDROID__)
        std::cerr << "android ANativeWindow=" << reinterpret_cast<void*>(
                static_cast<uintptr_t>(m_native.window))
            << " client=" << m_clientW << "x" << m_clientH;
#elif defined(__OHOS__)
        std::cerr << "ohos OHNativeWindow=" << reinterpret_cast<void*>(
                static_cast<uintptr_t>(m_native.window))
            << " client=" << m_clientW << "x" << m_clientH;
#else
        std::cerr << "(no platform diagnostics available)";
#endif
        std::cerr << "\n";
    }
}
