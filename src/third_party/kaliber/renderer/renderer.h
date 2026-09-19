#ifndef ENGINE_RENDERER_RENDERER_H
#define ENGINE_RENDERER_RENDERER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "third_party/kaliber/base/vecmath.h"
#include "third_party/kaliber/renderer/renderer_types.h"

namespace eng {

class ShaderSource;
class Platform;

enum class RendererType { kUnknown, kVulkan, kOpenGL };

class Renderer {
 public:
  using ResourceId = uint32_t;

  // Sentinel value for uninitialized or invalid resource handles.
  static constexpr ResourceId kInvalidId = 0;

  // Maximum number of texture units that can be bound simultaneously.
  static const unsigned kMaxTextureUnits = 8;

  // Factory method. Creates a renderer of the given type. Falls back to OpenGL
  // if Vulkan initialization fails.
  static std::unique_ptr<Renderer> Create(RendererType type);

  Renderer() = default;
  virtual ~Renderer() = default;

  // Initialize the renderer for the given platform. Returns true on success.
  virtual bool Initialize(Platform* platform) = 0;

  // Shut down the renderer and release all GPU resources.
  virtual void Shutdown() = 0;

  // Returns true if the renderer has been successfully initialized.
  virtual bool IsInitialzed() const = 0;

  // Notify the renderer that the framebuffer has been resized.
  virtual void OnFramebufferResized(int width, int height) = 0;

  // Return the current framebuffer dimensions in pixels.
  virtual int GetFramebufferWidth() const = 0;
  virtual int GetFramebufferHeight() const = 0;

  // Set the viewport rectangle in pixels. Coordinates are bottom-left origin.
  virtual void SetViewport(int x, int y, int width, int height) = 0;

  // Reset the viewport to cover the full screen.
  virtual void ResetViewport() = 0;

  // Set the scissor rectangle in pixels. Fragments outside this rectangle are
  // discarded. Coordinates are bottom-left origin.
  virtual void SetScissor(int x, int y, int width, int height) = 0;

  // Reset the scissor test to cover the full screen (effectively disabling it).
  virtual void ResetScissor() = 0;

  // Create an empty geometry resource with the given primitive type, vertex
  // layout, and optional index type. Data must be uploaded separately via
  // UpdateGeometry().
  virtual ResourceId CreateGeometry(
      Primitive primitive,
      VertexDescription vertex_description,
      DataType index_description = kDataType_Invalid) = 0;

  // Upload vertex and index data to an existing geometry resource. The
  // pointed-to data must remain valid for the duration of the current frame.
  virtual void UpdateGeometry(ResourceId resource_id,
                              size_t num_vertices,
                              const void* vertices,
                              size_t num_indices,
                              const void* indices) = 0;

  // Destroy a geometry resource and free its GPU memory.
  virtual void DestroyGeometry(ResourceId resource_id) = 0;

  // Issue a draw call for the given geometry. If |num_indices| is 0, all
  // indices (or vertices) are drawn. |start_offset| is the index offset into
  // the index (or vertex) buffer.
  virtual void Draw(ResourceId resource_id,
                    size_t num_indices = 0,
                    size_t start_offset = 0) = 0;

  // Create an empty texture resource. Data must be uploaded via
  // UpdateTexture().
  virtual ResourceId CreateTexture() = 0;

  // Upload raw image data to a texture resource. The pointed-to data must
  // remain valid for the duration of the current frame.
  virtual void UpdateTexture(ResourceId resource_id,
                             int width,
                             int height,
                             ImageFormat format,
                             size_t data_size,
                             uint8_t* image_data) = 0;

  // Upload a sub-region of image data to an existing texture resource.
  // |x_offset|, |y_offset| are the destination coordinates within the texture.
  // |width|, |height| are the dimensions of the sub-region.
  // |src_pitch| is the byte stride between rows in the source data. For
  // uncompressed formats this is the stride between pixel rows. For compressed
  // formats this is the stride between block rows. The pointed-to data must
  // remain valid for the duration of the current frame.
  virtual void UpdateTextureSubRegion(ResourceId resource_id,
                                      int x_offset,
                                      int y_offset,
                                      int width,
                                      int height,
                                      ImageFormat format,
                                      int src_pitch,
                                      uint8_t* image_data) = 0;

  // Destroy a texture resource and free its GPU memory.
  virtual void DestroyTexture(ResourceId resource_id) = 0;

  // Bind a texture to the given texture unit for subsequent draw calls.
  virtual void ActivateTexture(ResourceId resource_id, size_t texture_unit) = 0;

  // Create a shader program from the given source. |vertex_description| and
  // |primitive| describe the expected input geometry. |enable_depth_test|
  // controls whether depth testing is enabled when this shader is active.
  // Takes ownership of |source|.
  virtual ResourceId CreateShader(std::unique_ptr<ShaderSource> source,
                                  const VertexDescription& vertex_description,
                                  Primitive primitive,
                                  bool enable_depth_test) = 0;

  // Destroy a shader program.
  virtual void DestroyShader(ResourceId resource_id) = 0;

  // Bind a shader program for subsequent draw calls.
  virtual void ActivateShader(ResourceId resource_id) = 0;

  // Set a uniform variable on the given shader. The shader does not need to be
  // active when this is called.
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          const base::Vector2f& val) = 0;
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          const base::Vector3f& val) = 0;
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          const base::Vector4f& val) = 0;
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          const base::Matrix4f& val) = 0;
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          float val) = 0;
  virtual void SetUniform(ResourceId resource_id,
                          const std::string& name,
                          int val) = 0;

  // Set the color used to clear the screen at the start of each frame.
  virtual void SetClearColor(const base::Vector4f& color) = 0;

  // Begin a new frame. Must be called before any draw calls.
  virtual void PrepareForDrawing() = 0;

  // Finish the current frame and present it to the screen.
  virtual void Present() = 0;

  // Redirect subsequent draw calls to the given texture instead of the screen.
  virtual void BeginRenderToTexture(ResourceId texture_id) = 0;

  // Stop rendering to texture and resume rendering to the screen.
  virtual void EndRenderToTexture(ResourceId texture_id) = 0;

  // Query supported texture compression formats.
  bool SupportsETC1() const { return texture_compression_.etc1; }
  bool SupportsDXT1() const {
    return texture_compression_.dxt1 || texture_compression_.s3tc;
  }
  bool SupportsDXT5() const { return texture_compression_.s3tc; }
  bool SupportsATC() const { return texture_compression_.atc; }

  // Return a human-readable name for this renderer backend (e.g. "OpenGL").
  virtual const char* GetDebugName() = 0;

  // Return the type of this renderer.
  virtual RendererType GetRendererType() { return RendererType::kUnknown; }

  // Return the list of available GPU device names (Vulkan only).
  virtual std::vector<std::string> GetAvailableGpus() const { return {}; }

  // Return the index of the currently selected GPU.
  virtual int GetSelectedGpuIndex() const { return 0; }

  // Set the preferred GPU by device name. Must be called before Initialize().
  virtual void SetPreferredGpu(const std::string&) {}

 protected:
  struct TextureCompression {
    unsigned etc1 : 1;
    unsigned dxt1 : 1;
    unsigned latc : 1;
    unsigned s3tc : 1;
    unsigned pvrtc : 1;
    unsigned atc : 1;

    TextureCompression()
        : etc1(false),
          dxt1(false),
          latc(false),
          s3tc(false),
          pvrtc(false),
          atc(false) {}
  };

  TextureCompression texture_compression_;

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
};

}  // namespace eng

#endif  // ENGINE_RENDERER_RENDERER_H
