#pragma once

#include "context/vulkan_context.h"
#include "cef/cef_client.h"  // For AcceleratedPaintInfo
#include <mutex>
#include <chrono>

class VulkanCompositor {
public:
    VulkanCompositor();
    ~VulkanCompositor();

    bool init(VulkanContext* vk, uint32_t width, uint32_t height);
    void cleanup();

    // Update overlay texture from CEF buffer (BGRA) - software path
    // Just copies to staging buffer, call flushOverlay() to upload to GPU
    void updateOverlay(const void* data, int width, int height);

    // Get direct pointer to staging buffer for zero-copy writes
    // Returns nullptr if size doesn't match or not initialized
    void* getStagingBuffer(int width, int height);
    void markStagingDirty() { staging_pending_ = true; }
    bool hasPendingContent() const { return staging_pending_; }

    // Flush pending overlay data to GPU (call from render loop with active command buffer)
    // Returns true if there was data to flush
    bool flushOverlay(VkCommandBuffer cmd);

    // Update overlay from DMA-BUF - hardware accelerated path
    // Copies to local buffer and releases DMA-BUF immediately
    bool updateOverlayFromDmaBuf(const AcceleratedPaintInfo& info);

    // Composite overlay onto swapchain image
    // Call after mpv has rendered to the image
    void composite(VkCommandBuffer cmd, VkImage target, VkImageView targetView,
                   uint32_t width, uint32_t height, float alpha);

    // Resize resources immediately (we own the local buffer)
    void resize(uint32_t width, uint32_t height);

    // Check if we have valid content to composite
    bool hasValidOverlay() const { return has_content_; }

private:
    bool createLocalImage();
    bool createPipeline();
    bool createDescriptorSets();
    void destroyLocalImage();

    VulkanContext* vk_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // Local image we own and sample from (both software and DMA-BUF paths copy here)
    VkImage local_image_ = VK_NULL_HANDLE;
    VkDeviceMemory local_memory_ = VK_NULL_HANDLE;
    VkImageView local_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool has_content_ = false;

    // Staging buffer for software texture upload
    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    void* staging_mapped_ = nullptr;
    bool staging_pending_ = false;  // Data waiting to be flushed to GPU

    // DMA-BUF support
    bool dmabuf_supported_ = true;  // Set false if import fails

    // Thread safety - CEF paint callbacks come from different thread
    std::mutex mutex_;

    // Skip DMA-BUF imports briefly after resize (implicit sync causes hangs)
    std::chrono::steady_clock::time_point last_resize_time_;

    // Render pass and pipeline for compositing
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Descriptor set for overlay texture
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    // Push constants
    struct PushConstants {
        float alpha;
        float padding[3];
    };
};
