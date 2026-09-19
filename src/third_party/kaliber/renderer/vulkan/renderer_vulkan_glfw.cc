#include "third_party/kaliber/renderer/vulkan/renderer_vulkan.h"

#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/glfw/glfw/include/GLFW/glfw3.h"

namespace eng {

bool RendererVulkan::Initialize(Platform* platform) {
  DLOG(0) << "Initializing renderer.";

  int w, h;
  glfwGetFramebufferSize(platform->GetWindow(), &w, &h);

  if (!context_.Initialize()) {
    DLOG(0) << "Failed to initialize Vulkan context.";
    return false;
  }
  if (!context_.CreateSurface(platform->GetWindow(), w, h)) {
    DLOG(0) << "Vulkan context failed to create window.";
    return false;
  }

  return InitializeInternal();
}

}  // namespace eng
