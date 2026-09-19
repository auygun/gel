#include "third_party/kaliber/renderer/renderer.h"

#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/renderer/opengl/renderer_opengl.h"
#include "third_party/kaliber/renderer/vulkan/renderer_vulkan.h"

namespace eng {

// static
std::unique_ptr<Renderer> Renderer::Create(RendererType type) {
  std::unique_ptr<Renderer> renderer;
  if (type == RendererType::kVulkan) {
    renderer = std::make_unique<RendererVulkan>();
  } else if (type == RendererType::kOpenGL) {
    renderer = std::make_unique<RendererOpenGL>();
  } else {
    NOTREACHED();
  }
  return renderer;
}

}  // namespace eng
