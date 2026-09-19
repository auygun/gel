#include "third_party/kaliber/renderer/opengl/renderer_opengl.h"

#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/glfw/glfw/include/GLFW/glfw3.h"

using namespace base;

namespace eng {

bool RendererOpenGL::Initialize(Platform* platform) {
  DLOG(0) << "Initializing renderer.";

  window_ = platform->GetWindow();
  glfwMakeContextCurrent(window_);

  if (!gladLoadGLES2(glfwGetProcAddress)) {
    DLOG(0) << "Couldn't initialize OpenGL loader.";
    return false;
  }

  glfwSwapInterval(1);

  int w, h;
  glfwGetFramebufferSize(window_, &w, &h);
  OnFramebufferResized(w, h);

  return InitCommon();
}

void RendererOpenGL::OnDestroy() {}

void RendererOpenGL::Shutdown() {
  DLOG(0) << "Shutting down renderer.";
  is_initialized_ = false;
}

void RendererOpenGL::Present() {
  glfwSwapBuffers(window_);

  active_shader_id_ = 0;
  active_texture_id_ = {};
}

}  // namespace eng
