#include "third_party/kaliber/renderer/opengl/renderer_opengl.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_set>

#include "third_party/kaliber/base/hash.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/base/vecmath.h"
#include "third_party/kaliber/renderer/shader_source.h"

using namespace base;

namespace eng {

namespace {

constexpr GLenum kGlPrimitive[eng::kPrimitive_Max] = {GL_TRIANGLES,
                                                      GL_TRIANGLE_STRIP};

constexpr GLenum kGlDataType[eng::kDataType_Max] = {
    GL_UNSIGNED_BYTE, GL_FLOAT,        GL_INT,
    GL_SHORT,         GL_UNSIGNED_INT, GL_UNSIGNED_SHORT};

const std::string kAttributeNames[eng::kAttribType_Max] = {
    "in_color", "in_normal", "in_position", "in_tex_coord"};

}  // namespace

RendererOpenGL::RendererOpenGL() = default;

RendererOpenGL::~RendererOpenGL() {
  Shutdown();
  OnDestroy();
}

void RendererOpenGL::OnFramebufferResized(int width, int height) {
  framebuffer_width_ = width;
  framebuffer_height_ = height;
}

int RendererOpenGL::GetFramebufferWidth() const {
  return framebuffer_width_;
}

int RendererOpenGL::GetFramebufferHeight() const {
  return framebuffer_height_;
}

void RendererOpenGL::SetViewport(int x, int y, int width, int height) {
  glViewport(0, 0, width, height);
}

void RendererOpenGL::ResetViewport() {
  glViewport(0, 0, framebuffer_width_, framebuffer_height_);
}

void RendererOpenGL::SetScissor(int x, int y, int width, int height) {
  glScissor(x, framebuffer_height_ - y - height, width, height);
  glEnable(GL_SCISSOR_TEST);
}

void RendererOpenGL::ResetScissor() {
  glDisable(GL_SCISSOR_TEST);
}

Renderer::ResourceId RendererOpenGL::CreateGeometry(
    Primitive primitive,
    VertexDescription vertex_description,
    DataType index_description) {
  // Verify that we have a valid layout and get the total byte size per vertex.
  GLuint vertex_size = static_cast<GLuint>(GetVertexSize(vertex_description));
  if (!vertex_size) {
    DLOG(0) << "Invalid vertex layout";
    return kInvalidId;
  }

  GLuint vertex_array_id = 0;
  glGenVertexArrays(1, &vertex_array_id);
  glBindVertexArray(vertex_array_id);

  // Create the vertex buffer.
  GLuint vertex_buffer_id = 0;
  glGenBuffers(1, &vertex_buffer_id);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_id);

  if (!SetupVertexLayout(vertex_description, vertex_size)) {
    DLOG(0) << "Invalid vertex layout";
    return kInvalidId;
  }

  // Create the index buffer.
  GLuint index_buffer_id = 0;
  if (index_description != kDataType_Invalid) {
    glGenBuffers(1, &index_buffer_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_id);
  }

  glBindVertexArray(0);

  ResourceId resource_id = ++last_resource_id_;
  GLenum gl_index_type = index_description != kDataType_Invalid
                             ? kGlDataType[index_description]
                             : 0;
  geometries_[resource_id] = {0,
                              0,
                              kGlPrimitive[primitive],
                              gl_index_type,
                              vertex_size,
                              vertex_array_id,
                              vertex_buffer_id,
                              (GLuint)GetIndexSize(index_description),
                              index_buffer_id};
  return resource_id;
}

void RendererOpenGL::UpdateGeometry(ResourceId resource_id,
                                    size_t num_vertices,
                                    const void* vertices,
                                    size_t num_indices,
                                    const void* indices) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  // Go with GL_STATIC_DRAW for the first update.
  GLenum usage = it->second.num_vertices > 0 ? GL_STREAM_DRAW : GL_STATIC_DRAW;

  // Upload the vertex data.
  glBindBuffer(GL_ARRAY_BUFFER, it->second.vertex_buffer_id);
  glBufferData(GL_ARRAY_BUFFER, num_vertices * it->second.vertex_size, vertices,
               usage);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  it->second.num_vertices = (GLsizei)num_vertices;

  // Upload the index data.
  if (it->second.index_buffer_id) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.index_buffer_id);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_indices * it->second.index_size,
                 indices, usage);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    it->second.num_indices = (GLsizei)num_indices;
  }
}

void RendererOpenGL::DestroyGeometry(ResourceId resource_id) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  if (it->second.index_buffer_id)
    glDeleteBuffers(1, &(it->second.index_buffer_id));
  if (it->second.vertex_buffer_id)
    glDeleteBuffers(1, &(it->second.vertex_buffer_id));
  if (it->second.vertex_array_id)
    glDeleteVertexArrays(1, &(it->second.vertex_array_id));

  geometries_.erase(it);
}

void RendererOpenGL::Draw(ResourceId resource_id,
                          size_t num_indices,
                          size_t start_offset) {
  auto it = geometries_.find(resource_id);
  if (it == geometries_.end())
    return;

  if (num_indices == 0)
    num_indices = it->second.num_indices;

  glBindVertexArray(it->second.vertex_array_id);

  if (num_indices > 0)
    glDrawElements(it->second.primitive, static_cast<GLsizei>(num_indices),
                   it->second.index_type,
                   (void*)(intptr_t)(start_offset * sizeof(unsigned short)));
  else
    glDrawArrays(it->second.primitive, 0, it->second.num_vertices);

  glBindVertexArray(0);
}

Renderer::ResourceId RendererOpenGL::CreateTexture() {
  GLuint gl_id = 0;
  glGenTextures(1, &gl_id);
  glBindTexture(GL_TEXTURE_2D, gl_id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  ResourceId resource_id = ++last_resource_id_;
  textures_[resource_id] = {gl_id, 0, 0, 0};
  return resource_id;
}

void RendererOpenGL::UpdateTexture(ResourceId resource_id,
                                   int width,
                                   int height,
                                   ImageFormat format,
                                   size_t data_size,
                                   uint8_t* image_data) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  glBindTexture(GL_TEXTURE_2D, it->second.id);
  if (IsCompressedFormat(format)) {
    GLenum gl_format = 0;
    switch (format) {
      case ImageFormat::kDXT1:
        gl_format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        break;
      case ImageFormat::kDXT5:
        gl_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        break;
      case ImageFormat::kETC1:
        gl_format = GL_ETC1_RGB8_OES;
        break;
      default:
        NOTREACHED() << "- Unhandled texture format: "
                     << ImageFormatToString(format);
    }

    glCompressedTexImage2D(GL_TEXTURE_2D, 0, gl_format, width, height, 0,
                           static_cast<GLsizei>(data_size), image_data);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
      DLOG(0) << "GL ERROR after glCompressedTexImage2D: " << (int)err;
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image_data);
  }

  it->second.width = width;
  it->second.height = height;
}

void RendererOpenGL::UpdateTextureSubRegion(ResourceId resource_id,
                                            int x_offset,
                                            int y_offset,
                                            int width,
                                            int height,
                                            ImageFormat format,
                                            int src_pitch,
                                            uint8_t* image_data) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  glBindTexture(GL_TEXTURE_2D, it->second.id);
  if (IsCompressedFormat(format)) {
    GLenum gl_format = 0;
    int block_bytes = 0;
    switch (format) {
      case ImageFormat::kDXT1:
        gl_format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        block_bytes = 8;
        break;
      case ImageFormat::kDXT5:
        gl_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        block_bytes = 16;
        break;
      case ImageFormat::kETC1:
        gl_format = GL_ETC1_RGB8_OES;
        block_bytes = 8;
        break;
      default:
        NOTREACHED() << "- Unhandled texture format: "
                     << ImageFormatToString(format);
        return;
    }

    int blocks_x = (width + 3) / 4;
    int blocks_y = (height + 3) / 4;
    int row_bytes = blocks_x * block_bytes;
    GLsizei data_size = row_bytes * blocks_y;

    // glCompressedTexSubImage2D has no row length control, so pack block rows
    // into a contiguous buffer when the source pitch differs.
    if (src_pitch == row_bytes) {
      glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, y_offset, width,
                                height, gl_format, data_size, image_data);
    } else {
      auto packed = std::make_unique<uint8_t[]>(data_size);
      for (int r = 0; r < blocks_y; ++r)
        memcpy(packed.get() + static_cast<ptrdiff_t>(r) * row_bytes,
               image_data + static_cast<ptrdiff_t>(r) * src_pitch, row_bytes);
      glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, y_offset, width,
                                height, gl_format, data_size, packed.get());
    }
  } else {
    glPixelStorei(GL_UNPACK_ROW_LENGTH, src_pitch / 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, y_offset, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }
}

void RendererOpenGL::DestroyTexture(ResourceId resource_id) {
  auto it = textures_.find(resource_id);
  if (it == textures_.end())
    return;

  glDeleteTextures(1, &it->second.id);
  glDeleteFramebuffers(1, &it->second.frame_buffer);
  textures_.erase(it);
}

void RendererOpenGL::ActivateTexture(ResourceId resource_id,
                                     size_t texture_unit) {
  if (texture_unit >= kMaxTextureUnits) {
    DLOG(0) << "Invalid texture unit " << texture_unit;
    return;
  }

  auto it = textures_.find(resource_id);
  if (it == textures_.end()) {
    return;
  }

  if (it->second.id != active_texture_id_[texture_unit]) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(texture_unit));
    glBindTexture(GL_TEXTURE_2D, it->second.id);
    active_texture_id_[texture_unit] = it->second.id;
  }
}

Renderer::ResourceId RendererOpenGL::CreateShader(
    std::unique_ptr<ShaderSource> source,
    const VertexDescription& vertex_description,
    Primitive primitive,
    bool enable_depth_test) {
  GLuint vertex_shader =
      CreateShader(source->GetVertexSource(), GL_VERTEX_SHADER);
  if (!vertex_shader)
    return 0;

  GLuint fragment_shader =
      CreateShader(source->GetFragmentSource(), GL_FRAGMENT_SHADER);
  if (!fragment_shader)
    return 0;

  GLuint id = glCreateProgram();
  if (id) {
    glAttachShader(id, vertex_shader);
    glAttachShader(id, fragment_shader);
    if (!BindAttributeLocation(id, vertex_description)) {
      glDeleteProgram(id);
      return 0;
    }

    glLinkProgram(id);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(id, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
      GLint length = 0;
      glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
      if (length > 0) {
        char* buffer = (char*)malloc(length);
        if (buffer) {
          glGetProgramInfoLog(id, length, NULL, buffer);
          DLOG(0) << "Could not link program:\n" << buffer;
          free(buffer);
        }
      }
      glDeleteProgram(id);
      return 0;
    }
  }

  ResourceId resource_id = ++last_resource_id_;
  shaders_[resource_id] = {id, {}, enable_depth_test};
  return resource_id;
}

void RendererOpenGL::DestroyShader(ResourceId resource_id) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  glDeleteProgram(it->second.id);
  shaders_.erase(it);
}

void RendererOpenGL::ActivateShader(ResourceId resource_id) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  if (it->second.id != active_shader_id_) {
    glUseProgram(it->second.id);
    active_shader_id_ = it->second.id;
    if (it->second.enable_depth_test)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);
  }
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector2f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniform2fv(index, 1, val.GetData());
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector3f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniform3fv(index, 1, val.GetData());
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Vector4f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniform4fv(index, 1, val.GetData());
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                const base::Matrix4f& val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniformMatrix4fv(index, 1, GL_FALSE, val.GetData());
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                float val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniform1f(index, val);
}

void RendererOpenGL::SetUniform(ResourceId resource_id,
                                const std::string& name,
                                int val) {
  auto it = shaders_.find(resource_id);
  if (it == shaders_.end())
    return;

  GLint index = GetUniformLocation(it->second.id, name, it->second.uniforms);
  if (index >= 0)
    glUniform1i(index, val);
}

void RendererOpenGL::SetClearColor(const base::Vector4f& color) {
  glClearColor(color.x, color.y, color.z, color.w);
}

void RendererOpenGL::PrepareForDrawing() {
  glViewport(0, 0, framebuffer_width_, framebuffer_height_);
  glDisable(GL_SCISSOR_TEST);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RendererOpenGL::BeginRenderToTexture(ResourceId texture_id) {
  auto it = textures_.find(texture_id);
  if (it == textures_.end()) {
    return;
  }

  if (!it->second.frame_buffer) {
    glGenFramebuffers(1, &it->second.frame_buffer);
    glBindFramebuffer(GL_FRAMEBUFFER, it->second.frame_buffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           it->second.id, 0);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, it->second.frame_buffer);
  }

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    DLOG(0) << "Framebuffer is not complete! " << status;
    return;
  }

  glViewport(0, 0, it->second.width, it->second.height);
}

void RendererOpenGL::EndRenderToTexture(ResourceId texture_id) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, framebuffer_width_, framebuffer_height_);
}

bool RendererOpenGL::InitCommon() {
  // Get information about the currently active context.
  const char* renderer =
      reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

  DLOG(0) << "OpenGL:";
  DLOG(0) << "  vendor:         " << (const char*)glGetString(GL_VENDOR);
  DLOG(0) << "  renderer:       " << renderer;
  DLOG(0) << "  version:        " << version;
  DLOG(0) << "  shader version: "
          << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
  DLOG(0) << "Framebuffer size: " << framebuffer_width_ << ", "
          << framebuffer_height_;

  // Setup extensions.
  std::stringstream stream((const char*)glGetString(GL_EXTENSIONS));
  std::string token;
  std::unordered_set<std::string> extensions;
  while (std::getline(stream, token, ' '))
    extensions.insert(token);

#if 0
  DLOG(0) << "  extensions:";
  for (auto& ext : extensions)
    DLOG(0) << "    " << ext.c_str();
#endif

  // Check for supported texture compression extensions.
  if (extensions.find("GL_OES_compressed_ETC1_RGB8_texture") !=
      extensions.end())
    texture_compression_.etc1 = true;
  if (extensions.find("GL_EXT_texture_compression_dxt1") != extensions.end())
    texture_compression_.dxt1 = true;
  if (extensions.find("GL_EXT_texture_compression_latc") != extensions.end())
    texture_compression_.latc = true;
  if (extensions.find("GL_EXT_texture_compression_s3tc") != extensions.end())
    texture_compression_.s3tc = true;
  if (extensions.find("GL_IMG_texture_compression_pvrtc") != extensions.end())
    texture_compression_.pvrtc = true;
  if (extensions.find("GL_AMD_compressed_ATC_texture") != extensions.end() ||
      extensions.find("GL_ATI_texture_compression_atitc") != extensions.end())
    texture_compression_.atc = true;

  DLOG(0) << "TextureCompression:";
  DLOG(0) << "  atc:   " << texture_compression_.atc;
  DLOG(0) << "  dxt1:  " << texture_compression_.dxt1;
  DLOG(0) << "  etc1:  " << texture_compression_.etc1;
  DLOG(0) << "  s3tc:  " << texture_compression_.s3tc;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  is_initialized_ = true;

  return true;
}

void RendererOpenGL::DestroyAllResources() {
  std::vector<ResourceId> resource_ids;
  resource_ids.reserve(geometries_.size());
  for (auto& r : geometries_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyGeometry(r);

  resource_ids.clear();
  for (auto& r : shaders_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyShader(r);

  resource_ids.clear();
  for (auto& r : textures_)
    resource_ids.push_back(r.first);
  for (auto& r : resource_ids)
    DestroyTexture(r);

  DCHECK(geometries_.size() == 0);
  DCHECK(shaders_.size() == 0);
  DCHECK(textures_.size() == 0);
}

bool RendererOpenGL::SetupVertexLayout(const VertexDescription& vd,
                                       GLuint vertex_size) {
  GLuint attribute_index = 0;
  size_t vertex_offset = 0;

  for (auto& attr : vd) {
    if (attribute_index >= 16)
      return false;

    auto [attrib_type, data_type, num_elements, type_size] = attr;

    GLenum type = kGlDataType[data_type];

    glEnableVertexAttribArray(attribute_index);
    glVertexAttribPointer(attribute_index, static_cast<GLint>(num_elements),
                          type, GL_TRUE, static_cast<GLsizei>(vertex_size),
                          (const GLvoid*)vertex_offset);

    ++attribute_index;
    vertex_offset += num_elements * type_size;
  }
  return true;
}

GLuint RendererOpenGL::CreateShader(const char* source, GLenum type) {
  GLuint shader = glCreateShader(type);
  if (shader) {
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      GLint length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
      if (length) {
        char* buffer = (char*)malloc(length);
        if (buffer) {
          glGetShaderInfoLog(shader, length, NULL, buffer);
          DLOG(0) << "Could not compile shader " << type << ":\n" << buffer;
          free(buffer);
        }
        glDeleteShader(shader);
        shader = 0;
      }
    }
  }
  return shader;
}

bool RendererOpenGL::BindAttributeLocation(GLuint id,
                                           const VertexDescription& vd) {
  using namespace std::string_literals;

  int current = 0;
  int tex_coord = 0;

  for (auto& attr : vd) {
    AttribType attrib_type = std::get<0>(attr);
    std::string attrib_name = kAttributeNames[attrib_type];
    if (attrib_type == kAttribType_TexCoord)
      attrib_name += "_"s + std::to_string(tex_coord++);
    glBindAttribLocation(id, current++, attrib_name.c_str());
  }
  return current > 0;
}

GLint RendererOpenGL::GetUniformLocation(
    GLuint id,
    const std::string& name,
    std::vector<std::pair<size_t, GLuint>>& uniforms) {
  // Check if we've encountered this uniform before.
  auto hash = KR2Hash(name);
  auto it = std::find_if(uniforms.begin(), uniforms.end(),
                         [&](auto& r) { return hash == std::get<0>(r); });
  GLint index;
  if (it != uniforms.end()) {
    // Yes, we already have the mapping.
    index = std::get<1>(*it);
  } else {
    // No, ask the driver for the mapping and save it.
    index = glGetUniformLocation(id, name.c_str());
    if (index >= 0) {
      DCHECK(std::find_if(uniforms.begin(), uniforms.end(),
                          [&](auto& r) { return hash == std::get<0>(r); }) ==
             uniforms.end())
          << "Hash collision";
      uniforms.emplace_back(hash, index);
    } else {
      DLOG(0) << "Cannot find uniform " << name.c_str() << " (shader: " << id
              << ")";
    }
  }
  return index;
}

}  // namespace eng
