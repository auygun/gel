#include "third_party/kaliber/renderer/shader_source.h"

#include <cstring>

#include "third_party/kaliber/base/log.h"

namespace {

// Helper macros for our glsl shaders. Makes it possible to write generic code
// that compiles both for OpenGL and Vulkan.

const char kVertexShaderMacros[] = R"(#if defined(VULKAN)
#define UNIFORM_BEGIN layout(push_constant) uniform Params {
#define UNIFORM_V(X) X;
#define UNIFORM_F(X) X;
#define UNIFORM_END } params;
#define IN(X) layout(location = X) in
#define OUT(X) layout(location = X) out
#define PARAM(X) params.X
#else
#define UNIFORM_BEGIN
#define UNIFORM_V(X) uniform X;
#define UNIFORM_F(X)
#define UNIFORM_END
#define IN(X) attribute
#define OUT(X) varying
#define PARAM(X) X
#endif
)";

const char kFragmentShaderMacros[] = R"(#if defined(VULKAN)
#define UNIFORM_BEGIN layout(push_constant) uniform Params {
#define UNIFORM_V(X) X;
#define UNIFORM_F(X) X;
#define UNIFORM_END } params;
#define SAMPLER(N, X) layout(set = N, binding = 0) uniform X;
#define IN(X) layout(location = X) in
#define OUT(X) layout(location = X) out
#define FRAG_COLOR_OUT(X) layout(location = 0) out vec4 X;
#define FRAG_COLOR(X) X
#define PARAM(X) params.X
#define TEXTURE texture
#else
#define UNIFORM_BEGIN
#define UNIFORM_V(X)
#define UNIFORM_F(X) uniform X;
#define UNIFORM_END
#define SAMPLER(N, X) uniform X;
#define IN(X) varying
#define OUT(X) varying
#define FRAG_COLOR_OUT(X)
#define FRAG_COLOR(X) gl_FragColor
#define PARAM(X) X
#define TEXTURE texture2D
#endif
)";

template <size_t N>
constexpr size_t length(char const (&)[N]) {
  return N - 1;
}

constexpr size_t kVertexShaderMacrosLen = length(kVertexShaderMacros);
constexpr size_t kFragmentShaderMacrosLen = length(kFragmentShaderMacros);

}  // namespace

namespace eng {

bool ShaderSource::LoadFromMem(const std::string& name,
                               const char* vertex_source,
                               size_t vertex_source_len,
                               const char* fragment_source,
                               size_t fragment_source_len) {
  name_ = name;

  vertex_source_size_ =
      InjectMacros(vertex_source, vertex_source_len, vertex_source_,
                   kVertexShaderMacros, kVertexShaderMacrosLen);
  if (!vertex_source_)
    return false;

  fragment_source_size_ =
      InjectMacros(fragment_source, fragment_source_len, fragment_source_,
                   kFragmentShaderMacros, kFragmentShaderMacrosLen);
  if (!fragment_source_)
    return false;

  return true;
}

size_t ShaderSource::InjectMacros(const char* source,
                                  size_t source_len,
                                  std::unique_ptr<char[]>& dst,
                                  const char* inject,
                                  size_t inject_len) {
  // Include null-terminator.
  size_t total_size = inject_len + source_len + 1;
  dst = std::make_unique<char[]>(total_size);
  memcpy(dst.get(), inject, inject_len);
  memcpy(dst.get() + inject_len, source, source_len);
  dst[total_size - 1] = '\0';

  return total_size;
}

}  // namespace eng
