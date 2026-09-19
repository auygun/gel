#ifndef ENGINE_RENDERER_OPENGL_RENDERER_OPENGL_H
#define ENGINE_RENDERER_OPENGL_RENDERER_OPENGL_H

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "third_party/kaliber/renderer/renderer.h"
#include "third_party/glad/include/glad/gles2.h"

struct GLFWwindow;

namespace eng {

class RendererOpenGL final : public Renderer {
 public:
  RendererOpenGL();
  ~RendererOpenGL() final;

  bool Initialize(Platform* platform) final;
  void Shutdown() final;

  bool IsInitialzed() const final { return is_initialized_; }

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

  const char* GetDebugName() final { return "OpenGL"; }

  RendererType GetRendererType() final { return RendererType::kOpenGL; }

 private:
  struct GeometryOpenGL {
    GLsizei num_vertices = 0;
    GLsizei num_indices = 0;
    GLenum primitive = 0;
    GLenum index_type = 0;
    GLuint vertex_size = 0;
    GLuint vertex_array_id = 0;
    GLuint vertex_buffer_id = 0;
    GLuint index_size = 0;
    GLuint index_buffer_id = 0;
  };

  struct ShaderOpenGL {
    GLuint id = 0;
    std::vector<std::pair<size_t,  // Uniform name hash
                          GLuint   // Uniform index
                          >>
        uniforms;
    bool enable_depth_test = false;
  };

  struct TextureOpenGL {
    GLuint id = 0;
    GLuint frame_buffer = 0;
    int width = 0;
    int height = 0;
  };

  std::unordered_map<ResourceId, GeometryOpenGL> geometries_;
  std::unordered_map<ResourceId, ShaderOpenGL> shaders_;
  std::unordered_map<ResourceId, TextureOpenGL> textures_;
  ResourceId last_resource_id_ = 0;

  GLuint active_shader_id_ = 0;
  std::array<GLuint, kMaxTextureUnits> active_texture_id_ = {};

  bool is_initialized_ = false;

  int framebuffer_width_ = 0;
  int framebuffer_height_ = 0;

  GLFWwindow* window_ = nullptr;

  bool InitCommon();
  void ShutdownInternal();
  void OnDestroy();
  void DestroyAllResources();

  bool SetupVertexLayout(const VertexDescription& vd, GLuint vertex_size);
  GLuint CreateShader(const char* source, GLenum type);
  bool BindAttributeLocation(GLuint id, const VertexDescription& vd);
  GLint GetUniformLocation(GLuint id,
                           const std::string& name,
                           std::vector<std::pair<size_t, GLuint>>& uniforms);
};

}  // namespace eng

#endif  // ENGINE_RENDERER_OPENGL_RENDERER_OPENGL_H
