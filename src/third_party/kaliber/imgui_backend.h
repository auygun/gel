#ifndef ENGINE_IMGUI_BACKEND_H
#define ENGINE_IMGUI_BACKEND_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/renderer/renderer.h"

struct ImTextureData;

namespace eng {

class ImguiBackend {
 public:
  static constexpr float kBaseFontSize = 16.0f;

  ImguiBackend();
  ~ImguiBackend();

  // Sets the built-in font to use when no font path is given, or when the
  // requested font file cannot be loaded. Must be called before Initialize().
  void SetDefaultFont(const void* compressed_data, int compressed_size);

  void Initialize(Platform* platform,
                  const std::string& font_path = {},
                  int font_face_index = 0);
  void Shutdown();

  void RebuildFont(const std::string& font_path, int font_face_index);

  void CreateRenderResources(Renderer* renderer);

  std::pair<bool, bool> ProcessInput(Platform* platform);

  void NewFrame(float delta_time);
  void Draw();

  void SetGeometryChangedCallback(std::function<void()> cb) {
    on_geometry_changed_ = std::move(cb);
  }

 private:
  VertexDescription vertex_description_;
  std::vector<Renderer::ResourceId> geometries_;
  Renderer::ResourceId shader_ = Renderer::kInvalidId;
  Renderer* renderer_ = nullptr;
  Platform* platform_ = nullptr;
  size_t geometry_hash_ = 0;
  std::function<void()> on_geometry_changed_;
  // Resolved once: probing every script through fontconfig or CoreText costs
  // more than the rest of a font rebuild put together, and the answer cannot
  // change while the process runs.
  std::vector<Platform::FontInfo> fallback_fonts_;
  bool fallback_fonts_resolved_ = false;
  const void* default_font_data_ = nullptr;
  int default_font_size_ = 0;

  // Track InputText selection state for primary selection updates.
  unsigned int prev_sel_input_id_ = 0;
  int prev_sel_start_ = 0;
  int prev_sel_end_ = 0;

  void InitializeCommon(Platform* platform);
  void LoadFont(const std::string& font_path, int font_face_index);
  bool LoadFontFile(const std::string& font_path, int font_face_index);
  void LoadDefaultFont();
  void MergeFallbackFonts();
  void UpdatePrimarySelection();
  void UpdateTexture(ImTextureData* tex);
  void UpdateGeometries();
};

}  // namespace eng

#endif  // ENGINE_IMGUI_BACKEND_H
