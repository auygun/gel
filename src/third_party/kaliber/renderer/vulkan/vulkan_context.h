#ifndef ENGINE_RENDERER_VULKAN_VULKAN_CONTEXT_H
#define ENGINE_RENDERER_VULKAN_VULKAN_CONTEXT_H

#include <string>
#include <vector>

// volk.h must be included before vk_mem_alloc.h because VMA 3.3.0 checks
// VOLK_HEADER_VERSION (defined by volk.h) to enable its volk integration.
#include "third_party/volk/volk/volk.h"

#include "third_party/vma/vma/include/vk_mem_alloc.h"

struct GLFWwindow;

namespace eng {

class VulkanContext {
 public:
  VulkanContext();
  ~VulkanContext();

  bool Initialize();
  void Shutdown();

  bool CreateSurface(GLFWwindow* window, int width, int height);

  void ResizeSurface(int width, int height);
  void DestroySurface();

  VkFramebuffer GetFramebuffer();

  VkImage GetSwapchainImage();

  VkRenderPass GetRenderPass() { return window_.render_pass; }

  VkExtent2D GetSwapchainExtent() { return window_.swapchain_extent; }

  void AppendCommandBuffer(const VkCommandBuffer& command_buffer,
                           bool front = false);

  void Flush(bool all);

  bool PrepareBuffers();
  bool SwapBuffers();

  VkInstance GetInstance() { return instance_; }
  VkDevice GetDevice() { return device_; }
  VkPhysicalDevice GetPhysicalDevice() { return gpu_; }

  VmaAllocator GetAllocator() { return allocator_; }

  uint32_t GetSwapchainImageCount() const { return swapchain_image_count_; }

  uint32_t GetGraphicsQueue() const { return graphics_queue_family_index_; }

  VkFormat GetScreenFormat() const { return format_; }

  VkPhysicalDeviceProperties GetDeviceProperties() const { return gpu_props_; }

  int GetFramebufferWidth() const { return window_.width; }
  int GetFramebufferHeight() const { return window_.height; }

  const std::vector<std::string>& GetDeviceNames() const {
    return device_names_;
  }
  int GetSelectedDeviceIndex() const { return selected_device_index_; }
  void SetPreferredDevice(const std::string& name) {
    preferred_device_name_ = name;
  }

 private:
  enum { kMaxExtensions = 128, kMaxLayers = 64, kFrameLag = 2 };

  struct SwapchainImageResources {
    VkImage image;
    VkCommandBuffer graphics_to_present_cmd;
    VkImageView view;
    VkFramebuffer frame_buffer;
  };

  struct Window {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<SwapchainImageResources> swapchain_image_resources;
    uint32_t current_buffer = 0;
    int width = 0;
    int height = 0;
    VkCommandPool present_cmd_pool = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkExtent2D swapchain_extent = {0, 0};
  };

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VmaAllocator allocator_ = VK_NULL_HANDLE;

  VkPhysicalDeviceProperties gpu_props_;
  uint32_t queue_family_count_ = 0;
  std::vector<VkQueueFamilyProperties> queue_props_;

  bool queues_initialized_ = false;
  bool separate_present_queue_ = false;

  uint32_t graphics_queue_family_index_ = 0;
  uint32_t present_queue_family_index_ = 0;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;

  VkColorSpaceKHR color_space_;
  VkFormat format_;

  uint32_t frame_index_ = 0;

  VkSemaphore image_acquired_semaphores_[kFrameLag];
  VkSemaphore draw_complete_semaphores_[kFrameLag];
  VkSemaphore image_ownership_semaphores_[kFrameLag];
  VkFence fences_[kFrameLag];

  VkPhysicalDeviceMemoryProperties memory_properties_;
  VkPhysicalDeviceFeatures physical_device_features_;

  uint32_t swapchain_image_count_ = 0;

  std::vector<VkCommandBuffer> command_buffers_;

  Window window_;

  std::vector<std::string> device_names_;
  int selected_device_index_ = 0;
  std::string preferred_device_name_;

  // Extensions.
  uint32_t enabled_extension_count_ = 0;
  const char* extension_names_[kMaxExtensions];

  uint32_t enabled_layer_count_ = 0;
  const char* enabled_layers_[kMaxLayers];

  bool use_validation_layers_ = false;

  VkDebugUtilsMessengerEXT dbg_messenger_ = VK_NULL_HANDLE;

  bool CreateValidationLayers();
  bool InitializeExtensions();

  VkBool32 CheckLayers(uint32_t check_count,
                       const char** check_names,
                       const std::vector<VkLayerProperties>& instance_layers);

  static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
      VkDebugUtilsMessageTypeFlagsEXT message_type,
      const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
      void* user_data);

  bool CreatePhysicalDevice();

  bool InitializeQueues(VkSurfaceKHR surface);

  bool CreateDevice();

  bool CleanUpSwapChain(Window* window);

  bool UpdateSwapChain(Window* window);

  void DeinitSwapChainResources(Window* window);

  bool CreateSemaphores();

  void GetRequiredInstanceExtensions(const char**& extensions,
                                     uint32_t& count) const;

  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;
};

}  // namespace eng

#endif  // ENGINE_RENDERER_VULKAN_VULKAN_DEVICE_H
