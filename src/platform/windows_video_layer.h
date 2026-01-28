#pragma once
#ifdef _WIN32

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <windows.h>

struct SDL_Window;

class WindowsVideoLayer {
public:
    WindowsVideoLayer();
    ~WindowsVideoLayer();

    bool init(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
              VkDevice device, uint32_t queueFamily,
              const char* const* deviceExtensions, uint32_t deviceExtensionCount,
              const char* const* instanceExtensions);
    void cleanup();

    bool createSwapchain(uint32_t width, uint32_t height);
    void destroySwapchain();

    // Frame acquisition (matches WaylandSubsurface/MacOSVideoLayer interface)
    bool startFrame(VkImage* outImage, VkImageView* outView, VkFormat* outFormat);
    void submitFrame();

    // Accessors
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    VkDevice vkDevice() const { return device_; }
    VkInstance vkInstance() const { return instance_; }
    VkPhysicalDevice vkPhysicalDevice() const { return physical_device_; }
    VkQueue vkQueue() const { return queue_; }
    uint32_t vkQueueFamily() const { return queue_family_; }
    PFN_vkGetInstanceProcAddr vkGetProcAddr() const { return vkGetInstanceProcAddr; }
    const VkPhysicalDeviceFeatures2* features() const { return &features2_; }
    const char* const* deviceExtensions() const { return device_extensions_; }
    int deviceExtensionCount() const { return device_extension_count_; }

    void resize(uint32_t width, uint32_t height);
    void setVisible(bool visible);
    void setPosition(int x, int y);

    bool isHdr() const { return is_hdr_; }
    void setColorspace() {}  // Windows HDR is automatic via DXGI colorspace
    void setDestinationSize(int, int) {}  // no-op on Windows

private:
    static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    SDL_Window* parent_window_ = nullptr;
    HWND parent_hwnd_ = nullptr;
    HWND video_hwnd_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    static constexpr uint32_t MAX_IMAGES = 4;
    VkImage images_[MAX_IMAGES] = {};
    VkImageView image_views_[MAX_IMAGES] = {};
    uint32_t image_count_ = 0;
    uint32_t current_image_idx_ = 0;
    bool frame_active_ = false;

    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkFence acquire_fence_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool is_hdr_ = false;
    bool visible_ = false;

    // Features/extensions for mpv
    VkPhysicalDeviceFeatures2 features2_{};
    VkPhysicalDeviceVulkan11Features vk11_features_{};
    VkPhysicalDeviceVulkan12Features vk12_features_{};
    const char* const* device_extensions_ = nullptr;
    int device_extension_count_ = 0;
};

#endif // _WIN32
