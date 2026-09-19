#ifndef ENGINE_RENDERER_SHADER_SOURCE_H
#define ENGINE_RENDERER_SHADER_SOURCE_H

#include <memory>
#include <string>

namespace eng {

class ShaderSource {
 public:
  ShaderSource() = default;
  ~ShaderSource() = default;

  bool LoadFromMem(const std::string& name,
                   const char* vertex_source,
                   size_t vertex_source_len,
                   const char* fragment_source,
                   size_t fragment_source_len);

  const char* GetVertexSource() const { return vertex_source_.get(); }
  const char* GetFragmentSource() const { return fragment_source_.get(); }

  size_t vertex_source_size() const { return vertex_source_size_; }
  size_t fragment_source_size() const { return fragment_source_size_; }

  const std::string& name() const { return name_; }

 private:
  std::string name_;

  std::unique_ptr<char[]> vertex_source_;
  std::unique_ptr<char[]> fragment_source_;

  size_t vertex_source_size_ = 0;
  size_t fragment_source_size_ = 0;

  size_t InjectMacros(const char* source,
                      size_t source_len,
                      std::unique_ptr<char[]>& dst,
                      const char* inject,
                      size_t inject_len);
};

}  // namespace eng

#endif  // ENGINE_RENDERER_SHADER_SOURCE_H
