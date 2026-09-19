#ifndef ENGINE_RENDERER_VULKAN_RENDERER_VULKAN_H
#define ENGINE_RENDERER_VULKAN_RENDERER_VULKAN_H

#include <array>
#include <atomic>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "third_party/kaliber/renderer/vulkan/vulkan_context.h"

#include "third_party/kaliber/base/task_runner.h"
#include "third_party/kaliber/renderer/renderer.h"

namespace eng {

class RendererVulkan final : public Renderer {
 public:
  RendererVulkan();
  ~RendererVulkan() final;

  bool Initialize(Platform* platform) final;
  void Shutdown() final;

  bool IsInitialzed() const final { return device_ != VK_NULL_HANDLE; }

  void OnFramebufferResized(int width, int height) final;

  int GetFramebufferWidth() const final;
  int GetFramebufferHeight() const final;

  void SetViewport(int x, int y, int width, int height) final;
  void ResetViewport() final;

  void SetScissor(int x, int y, int width, int height) final;
  void ResetScissor() final;

  ResourceId CreateGeometry(
      Primitive primitive,
      VertexDescription vertex_description,
      DataType index_description = kDataType_Invalid) final;
  void UpdateGeometry(ResourceId resource_id,
                      size_t num_vertices,
                      const void* vertices,
                      size_t num_indices,
                      const void* indices) final;
  void DestroyGeometry(ResourceId resource_id) final;
  void Draw(ResourceId resource_id,
            size_t num_indices = 0,
            size_t start_offset = 0) final;

  ResourceId CreateTexture() final;
  void UpdateTexture(ResourceId resource_id,
                     int width,
                     int height,
                     ImageFormat format,
                     size_t data_size,
                     uint8_t* image_data) final;
  void UpdateTextureSubRegion(ResourceId resource_id,
                              int x_offset,
                              int y_offset,
                              int width,
                              int height,
                              ImageFormat format,
                              int src_pitch,
                              uint8_t* image_data) final;
  void DestroyTexture(ResourceId resource_id) final;
  void ActivateTexture(ResourceId resource_id, size_t texture_unit) final;

  ResourceId CreateShader(std::unique_ptr<ShaderSource> source,
                          const VertexDescription& vertex_description,
                          Primitive primitive,
                          bool enable_depth_test) final;
  void DestroyShader(ResourceId resource_id) final;
  void ActivateShader(ResourceId resource_id) final;

  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  const base::Vector2f& val) final;
  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  const base::Vector3f& val) final;
  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  const base::Vector4f& val) final;
  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  const base::Matrix4f& val) final;
  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  float val) final;
  void SetUniform(ResourceId resource_id,
                  const std::string& name,
                  int val) final;

  void SetClearColor(const base::Vector4f& color) final;
  void PrepareForDrawing() final;
  void Present() final;

  void BeginRenderToTexture(ResourceId texture_id) final;
  void EndRenderToTexture(ResourceId texture_id) final;

  const char* GetDebugName() final { return "Vulkan"; }

  RendererType GetRendererType() final { return RendererType::kVulkan; }

  std::vector<std::string> GetAvailableGpus() const final;
  int GetSelectedGpuIndex() const final;
  void SetPreferredGpu(const std::string& name) final;

 private:
  // VkBuffer or VkImage with allocator.
  template <typename T>
  using Buffer = std::tuple<T, VmaAllocation>;

  // VkDescriptorPool with usage count.
  using DescPool = std::tuple<VkDescriptorPool, size_t>;

  // VkDescriptorSet with the pool which it was allocated from.
  using DescSet = std::tuple<VkDescriptorSet, DescPool*>;

  // Containers to keep information of resources to be destroyed.
  using BufferDeathRow = std::vector<Buffer<VkBuffer>>;
  using ImageDeathRow =
      std::vector<std::tuple<Buffer<VkImage>, VkImageView, VkFramebuffer>>;
  using DescSetDeathRow = std::vector<DescSet>;
  using PipelineDeathRow =
      std::vector<std::tuple<VkPipeline, VkPipelineLayout>>;

  struct GeometryVulkan {
    Buffer<VkBuffer> buffer;
    size_t buffer_size = 0;
    uint32_t num_vertices = 0;
    uint32_t num_indices = 0;
    size_t vertex_size = 0;
    uint64_t index_data_offset = 0;
    uint64_t index_type_size = 0;
    VkIndexType index_type = VK_INDEX_TYPE_NONE_KHR;
  };

  struct ShaderVulkan {
    std::vector<std::tuple<size_t,  // Variable name hash
                           size_t,  // Variable size
                           size_t   // Push constant offset
                           >>
        variables;
    bool push_constants_dirty = false;
    std::unique_ptr<char[]> push_constants;
    size_t push_constants_size = 0;
    std::vector<std::string> sampler_uniform_names;
    size_t desc_set_count = 0;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  struct TextureVulkan {
    Buffer<VkImage> image;
    VkImageView view = VK_NULL_HANDLE;
    DescSet desc_set = {};
    int width = 0;
    int height = 0;
    VkFramebuffer frame_buffer_ = VK_NULL_HANDLE;
  };

  // Each frame contains 2 command buffers with separate synchronization scopes.
  // One for creating resources (recorded outside a render pass) and another for
  // drawing (recorded inside a render pass). Also contains list of resources to
  // be destroyed when the frame is cycled. There are 2 or 3 frames (double or
  // tripple buffering) that are cycled constantly.
  struct Frame {
    VkCommandPool setup_command_pool = VK_NULL_HANDLE;
    VkCommandBuffer setup_command_buffer = VK_NULL_HANDLE;
    VkCommandPool draw_command_pool = VK_NULL_HANDLE;
    VkCommandBuffer draw_command_buffer = VK_NULL_HANDLE;

    BufferDeathRow buffers_to_destroy;
    ImageDeathRow images_to_destroy;
    DescSetDeathRow desc_sets_to_destroy;
    PipelineDeathRow pipelines_to_destroy;
  };

  struct StagingBuffer {
    Buffer<VkBuffer> buffer{VK_NULL_HANDLE, nullptr};
    uint64_t frame_used = 0;
    uint32_t fill_amount = 0;
    VmaAllocationInfo alloc_info;
  };

  std::unordered_map<ResourceId, GeometryVulkan> geometries_;
  std::unordered_map<ResourceId, ShaderVulkan> shaders_;
  std::unordered_map<ResourceId, TextureVulkan> textures_;
  ResourceId last_resource_id_ = 0;

  VulkanContext context_;

  VkDevice device_ = VK_NULL_HANDLE;
  size_t frames_drawn_ = 0;
  std::vector<Frame> frames_;
  int current_frame_ = 0;

  std::vector<StagingBuffer> staging_buffers_;
  int current_staging_buffer_ = 0;
  uint32_t staging_buffer_size_ = 256 * 1024;
  uint64_t max_staging_buffer_size_ = uint64_t{16} * 1024 * 1024;
  bool staging_buffer_used_ = false;

  ResourceId active_shader_id_ = 0;

  std::vector<std::unique_ptr<DescPool>> desc_pools_;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> active_descriptor_sets_;

  VkSampler sampler_ = VK_NULL_HANDLE;

  VkClearColorValue clear_color_ = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderPass onscreen_clear_pass_ = VK_NULL_HANDLE;
  VkRenderPass onscreen_load_pass_ = VK_NULL_HANDLE;
  VkRenderPass offscreen_write_pass_ = VK_NULL_HANDLE;
  bool rendering_offscreen_ = false;
  bool frame_prepared_ = false;

  std::thread setup_thread_;
  base::TaskRunner task_runner_;
  std::counting_semaphore<> semaphore_{0};
  std::atomic<bool> quit_{false};

  bool InitializeInternal();

  void BeginFrame();

  void FlushSetupBuffer();

  void FreePendingResources(int frame);

  void MemoryBarrier(VkPipelineStageFlags src_stage_mask,
                     VkPipelineStageFlags dst_stage_mask,
                     VkAccessFlags src_access,
                     VkAccessFlags dst_access);
  void FullBarrier();

  bool AllocateStagingBuffer(uint32_t amount,
                             uint32_t segment,
                             uint32_t alignment,
                             uint32_t& alloc_offset,
                             uint32_t& alloc_size);
  bool InsertStagingBuffer();

  DescPool* AllocateDescriptorPool();
  void FreeDescriptorPool(DescPool* desc_pool);

  bool AllocateBuffer(Buffer<VkBuffer>& buffer,
                      uint32_t size,
                      uint32_t usage,
                      VmaMemoryUsage mapping);
  void FreeBuffer(Buffer<VkBuffer> buffer);
  void UpdateBuffer(VkBuffer buffer,
                    size_t offset,
                    const void* data,
                    size_t data_size);
  void BufferMemoryBarrier(VkBuffer buffer,
                           uint64_t from,
                           uint64_t size,
                           VkPipelineStageFlags src_stage_mask,
                           VkPipelineStageFlags dst_stage_mask,
                           VkAccessFlags src_access,
                           VkAccessFlags dst_access);

  bool AllocateImage(Buffer<VkImage>& image,
                     VkImageView& view,
                     DescSet& desc_set,
                     VkFormat format,
                     int width,
                     int height,
                     VkImageUsageFlags usage,
                     VmaMemoryUsage mapping);
  void FreeImage(Buffer<VkImage> image,
                 VkImageView image_view,
                 DescSet desc_set,
                 VkFramebuffer frame_buffer);
  void UpdateImage(VkImage image,
                   VkFormat format,
                   const uint8_t* data,
                   int width,
                   int height);
  void UpdateImageSubRegion(VkImage image,
                            VkFormat format,
                            const uint8_t* data,
                            int x_offset,
                            int y_offset,
                            int width,
                            int height,
                            int src_pitch);
  void ImageMemoryBarrier(VkCommandBuffer command_buffer,
                          VkImage image,
                          VkPipelineStageFlags src_stage_mask,
                          VkPipelineStageFlags dst_stage_mask,
                          VkAccessFlags src_access,
                          VkAccessFlags dst_access,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout);

  bool CreatePipelineLayout(ShaderVulkan& shader,
                            const std::vector<uint8_t>& spirv_vertex,
                            const std::vector<uint8_t>& spirv_fragment);

  void DrawListBegin(VkRenderPass render_pass);
  void DrawListEnd();

  void SwapBuffers();

  void SetupThreadMain();

  template <typename T>
  bool SetUniformInternal(ShaderVulkan& shader, const std::string& name, T val);

  bool IsFormatSupported(VkFormat format);

  void DestroyAllResources();
};

}  // namespace eng

#endif  // ENGINE_RENDERER_VULKAN_RENDERER_VULKAN_H
