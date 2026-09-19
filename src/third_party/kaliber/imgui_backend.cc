#include "third_party/kaliber/imgui_backend.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <memory>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/imgui/imgui/misc/freetype/imgui_freetype.h"
#include "third_party/kaliber/base/file.h"
#include "third_party/kaliber/base/hash.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/color_bitmap_font_loader.h"
#include "third_party/kaliber/input_codes.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/renderer/renderer.h"
#include "third_party/kaliber/renderer/shader_source.h"

using namespace base;

namespace eng {

namespace {
const char kVertexDescription[] = "p2f;t2f;c4b";

const char kImguiVertexShader[] = R"(IN(0) vec2 in_position;
IN(1) vec2 in_tex_coord_0;
IN(2) vec4 in_color;

UNIFORM_BEGIN
UNIFORM_V(mat4 projection)
UNIFORM_END

OUT(0) vec2 tex_coord_0;
OUT(1) vec4 color;

void main() {
  tex_coord_0 = in_tex_coord_0;
  color = in_color;

  gl_Position = PARAM(projection) * vec4(in_position, 0.0, 1.0);
}
)";

const char kImguiFragmentShader[] = R"(#ifdef GL_ES
precision mediump float;
#endif

IN(0) vec2 tex_coord_0;
IN(1) vec4 color;

UNIFORM_BEGIN
UNIFORM_V(mat4 projection)
UNIFORM_END

SAMPLER(0, sampler2D texture_0)

FRAG_COLOR_OUT(frag_color)

void main() {
  FRAG_COLOR(frag_color) = TEXTURE(texture_0, tex_coord_0) * color;
}
)";

static ImGuiKey TranslateKey(Key platform_key) {
  switch (platform_key) {
    // Letters
    case Key::A:
      return ImGuiKey_A;
    case Key::B:
      return ImGuiKey_B;
    case Key::C:
      return ImGuiKey_C;
    case Key::D:
      return ImGuiKey_D;
    case Key::E:
      return ImGuiKey_E;
    case Key::F:
      return ImGuiKey_F;
    case Key::G:
      return ImGuiKey_G;
    case Key::H:
      return ImGuiKey_H;
    case Key::I:
      return ImGuiKey_I;
    case Key::J:
      return ImGuiKey_J;
    case Key::K:
      return ImGuiKey_K;
    case Key::L:
      return ImGuiKey_L;
    case Key::M:
      return ImGuiKey_M;
    case Key::N:
      return ImGuiKey_N;
    case Key::O:
      return ImGuiKey_O;
    case Key::P:
      return ImGuiKey_P;
    case Key::Q:
      return ImGuiKey_Q;
    case Key::R:
      return ImGuiKey_R;
    case Key::S:
      return ImGuiKey_S;
    case Key::T:
      return ImGuiKey_T;
    case Key::U:
      return ImGuiKey_U;
    case Key::V:
      return ImGuiKey_V;
    case Key::W:
      return ImGuiKey_W;
    case Key::X:
      return ImGuiKey_X;
    case Key::Y:
      return ImGuiKey_Y;
    case Key::Z:
      return ImGuiKey_Z;

    // Main keyboard numbers (top row)
    case Key::Num0:
      return ImGuiKey_0;
    case Key::Num1:
      return ImGuiKey_1;
    case Key::Num2:
      return ImGuiKey_2;
    case Key::Num3:
      return ImGuiKey_3;
    case Key::Num4:
      return ImGuiKey_4;
    case Key::Num5:
      return ImGuiKey_5;
    case Key::Num6:
      return ImGuiKey_6;
    case Key::Num7:
      return ImGuiKey_7;
    case Key::Num8:
      return ImGuiKey_8;
    case Key::Num9:
      return ImGuiKey_9;

    // Function keys
    case Key::F1:
      return ImGuiKey_F1;
    case Key::F2:
      return ImGuiKey_F2;
    case Key::F3:
      return ImGuiKey_F3;
    case Key::F4:
      return ImGuiKey_F4;
    case Key::F5:
      return ImGuiKey_F5;
    case Key::F6:
      return ImGuiKey_F6;
    case Key::F7:
      return ImGuiKey_F7;
    case Key::F8:
      return ImGuiKey_F8;
    case Key::F9:
      return ImGuiKey_F9;
    case Key::F10:
      return ImGuiKey_F10;
    case Key::F11:
      return ImGuiKey_F11;
    case Key::F12:
      return ImGuiKey_F12;
    case Key::F13:
      return ImGuiKey_F13;
    case Key::F14:
      return ImGuiKey_F14;
    case Key::F15:
      return ImGuiKey_F15;
    case Key::F16:
      return ImGuiKey_F16;
    case Key::F17:
      return ImGuiKey_F17;
    case Key::F18:
      return ImGuiKey_F18;
    case Key::F19:
      return ImGuiKey_F19;
    case Key::F20:
      return ImGuiKey_F20;
    case Key::F21:
      return ImGuiKey_F21;
    case Key::F22:
      return ImGuiKey_F22;
    case Key::F23:
      return ImGuiKey_F23;
    case Key::F24:
      return ImGuiKey_F24;

    // Control keys
    case Key::Escape:
      return ImGuiKey_Escape;
    case Key::Space:
      return ImGuiKey_Space;
    case Key::Enter:
      return ImGuiKey_Enter;
    case Key::Tab:
      return ImGuiKey_Tab;
    case Key::Backspace:
      return ImGuiKey_Backspace;
    case Key::Insert:
      return ImGuiKey_Insert;
    case Key::Delete:
      return ImGuiKey_Delete;

    // Arrow/Navigation keys
    case Key::Up:
      return ImGuiKey_UpArrow;
    case Key::Down:
      return ImGuiKey_DownArrow;
    case Key::Left:
      return ImGuiKey_LeftArrow;
    case Key::Right:
      return ImGuiKey_RightArrow;
    case Key::PageUp:
      return ImGuiKey_PageUp;
    case Key::PageDown:
      return ImGuiKey_PageDown;
    case Key::Home:
      return ImGuiKey_Home;
    case Key::End:
      return ImGuiKey_End;

    // Modifier keys
    case Key::ShiftLeft:
      return ImGuiKey_LeftShift;
    case Key::ShiftRight:
      return ImGuiKey_RightShift;
    case Key::ControlLeft:
      return ImGuiKey_LeftCtrl;
    case Key::ControlRight:
      return ImGuiKey_RightCtrl;
    case Key::AltLeft:
      return ImGuiKey_LeftAlt;
    case Key::AltRight:
      return ImGuiKey_RightAlt;
    case Key::SuperLeft:
      return ImGuiKey_LeftSuper;
    case Key::SuperRight:
      return ImGuiKey_RightSuper;
    case Key::Menu:
      return ImGuiKey_Menu;

    // Punctuation / symbol keys
    case Key::Apostrophe:
      return ImGuiKey_Apostrophe;
    case Key::Comma:
      return ImGuiKey_Comma;
    case Key::Minus:
      return ImGuiKey_Minus;
    case Key::Period:
      return ImGuiKey_Period;
    case Key::Slash:
      return ImGuiKey_Slash;
    case Key::Semicolon:
      return ImGuiKey_Semicolon;
    case Key::Equal:
      return ImGuiKey_Equal;
    case Key::LeftBracket:
      return ImGuiKey_LeftBracket;
    case Key::Backslash:
      return ImGuiKey_Backslash;
    case Key::RightBracket:
      return ImGuiKey_RightBracket;
    case Key::GraveAccent:
      return ImGuiKey_GraveAccent;

    // Lock / misc keys
    case Key::CapsLock:
      return ImGuiKey_CapsLock;
    case Key::ScrollLock:
      return ImGuiKey_ScrollLock;
    case Key::NumLock:
      return ImGuiKey_NumLock;
    case Key::PrintScreen:
      return ImGuiKey_PrintScreen;
    case Key::Pause:
      return ImGuiKey_Pause;

    // Keypad
    case Key::Keypad0:
      return ImGuiKey_Keypad0;
    case Key::Keypad1:
      return ImGuiKey_Keypad1;
    case Key::Keypad2:
      return ImGuiKey_Keypad2;
    case Key::Keypad3:
      return ImGuiKey_Keypad3;
    case Key::Keypad4:
      return ImGuiKey_Keypad4;
    case Key::Keypad5:
      return ImGuiKey_Keypad5;
    case Key::Keypad6:
      return ImGuiKey_Keypad6;
    case Key::Keypad7:
      return ImGuiKey_Keypad7;
    case Key::Keypad8:
      return ImGuiKey_Keypad8;
    case Key::Keypad9:
      return ImGuiKey_Keypad9;
    case Key::KeypadDecimal:
      return ImGuiKey_KeypadDecimal;
    case Key::KeypadDivide:
      return ImGuiKey_KeypadDivide;
    case Key::KeypadMultiply:
      return ImGuiKey_KeypadMultiply;
    case Key::KeypadSubtract:
      return ImGuiKey_KeypadSubtract;
    case Key::KeypadAdd:
      return ImGuiKey_KeypadAdd;
    case Key::KeypadEnter:
      return ImGuiKey_KeypadEnter;
    case Key::KeypadEqual:
      return ImGuiKey_KeypadEqual;

    // Default case for unmapped keys
    case Key::Unknown:
    case Key::MaxKeys:
    default:
      return ImGuiKey_None;
  }
}

}  // namespace

ImguiBackend::ImguiBackend() = default;

ImguiBackend::~ImguiBackend() {
  Shutdown();
}

void ImguiBackend::SetDefaultFont(const void* compressed_data,
                                  int compressed_size) {
  default_font_data_ = compressed_data;
  default_font_size_ = compressed_size;
}

void ImguiBackend::LoadFont(const std::string& font_path, int font_face_index) {
  if (LoadFontFile(font_path, font_face_index))
    return;
  // The requested font could not be loaded, either because no font was
  // requested or because FreeType does not understand the file (e.g. Windows'
  // legacy .fon plotter fonts). Fall back to the built-in font so the UI stays
  // usable and MergeFallbackFonts() has a base font to merge into.
  LoadDefaultFont();
}

bool ImguiBackend::LoadFontFile(const std::string& font_path,
                                int font_face_index) {
  if (font_path.empty())
    return false;
  ImGuiIO& io = ImGui::GetIO();
  DLOG(0) << "Font: " << font_path;
  base::ScopedFILE fp(fopen(font_path.c_str(), "rb"));
  if (fp) {
    fseek(fp.get(), 0, SEEK_END);
    long file_size = ftell(fp.get());
    fseek(fp.get(), 0, SEEK_SET);

    // AddFontFromMemoryTTF() hard-asserts on anything this small (it cannot
    // hold a font header), and ftell() reports -1 on failure.
    if (file_size <= 100) {
      DLOG(0) << "Font file too small: " << font_path.c_str();
      return false;
    }

    void* buffer = IM_ALLOC(file_size);
    size_t bytes_read = fread(buffer, 1, file_size, fp.get());
    if (bytes_read == static_cast<size_t>(file_size)) {
      // Suppress ImGui error output during font loading. AddFont() validates
      // the data via FreeType and rolls back on failure, but fires
      // IM_ASSERT_USER_ERROR which logs noisy errors for fonts with
      // unsupported formats (e.g. Type 1, CFF2).
      int font_count = io.Fonts->Fonts.Size;
      auto enable_assert = io.ConfigErrorRecoveryEnableAssert;
      auto enable_debug_log = io.ConfigErrorRecoveryEnableDebugLog;
      auto enable_tooltip = io.ConfigErrorRecoveryEnableTooltip;
      io.ConfigErrorRecoveryEnableAssert = false;
      io.ConfigErrorRecoveryEnableDebugLog = false;
      io.ConfigErrorRecoveryEnableTooltip = false;
      ImFontConfig cfg;
      cfg.FontNo = static_cast<ImU32>(font_face_index);
      io.Fonts->AddFontFromMemoryTTF(buffer, (int)file_size, kBaseFontSize,
                                     &cfg);
      io.ConfigErrorRecoveryEnableAssert = enable_assert;
      io.ConfigErrorRecoveryEnableDebugLog = enable_debug_log;
      io.ConfigErrorRecoveryEnableTooltip = enable_tooltip;
      if (io.Fonts->Fonts.Size == font_count)
        DLOG(0) << "Unsupported font format: " << font_path.c_str();
      else
        return true;
    } else {
      DLOG(0) << "Failed to read font file: " << font_path.c_str();
      IM_FREE(buffer);
    }
  } else {
    DLOG(0) << "Failed to open font file: " << font_path.c_str();
  }
  return false;
}

void ImguiBackend::LoadDefaultFont() {
  ImGuiIO& io = ImGui::GetIO();
  if (default_font_data_)
    io.Fonts->AddFontFromMemoryCompressedTTF(default_font_data_,
                                             default_font_size_, kBaseFontSize);
  else
    io.Fonts->AddFontDefault();
}

void ImguiBackend::InitializeCommon(Platform* platform) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  if (!ParseVertexDescription(kVertexDescription, vertex_description_))
    DLOG(0) << "Failed to parse vertex description.";

  platform_ = platform;

  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  platform_io.Platform_SetClipboardTextFn = [](ImGuiContext*,
                                               const char* text) {
    // platform_ is stored as user data in the ImGui context.
    auto* p = static_cast<Platform*>(ImGui::GetIO().BackendPlatformUserData);
    p->SetClipboardText(text);
  };
  platform_io.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
    auto* p = static_cast<Platform*>(ImGui::GetIO().BackendPlatformUserData);
    return p->GetClipboardText();
  };
  io.BackendPlatformUserData = platform;

  io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
}

void ImguiBackend::Initialize(Platform* platform,
                              const std::string& font_path,
                              int font_face_index) {
  InitializeCommon(platform);
  LoadFont(font_path, font_face_index);
  MergeFallbackFonts();
}

void ImguiBackend::RebuildFont(const std::string& font_path,
                               int font_face_index) {
  ImGui::GetIO().Fonts->Clear();
  LoadFont(font_path, font_face_index);
  MergeFallbackFonts();
  // Reset FontSizeBase so ImGui picks up the new font's size on the next
  // frame. Without this, switching from a font with a different base size
  // (e.g. ImGui's 13px default after a failed load) leaves the old value
  // in place and the new font renders at the wrong scale.
  ImGui::GetStyle().FontSizeBase = 0.0f;
}

void ImguiBackend::MergeFallbackFonts() {
  if (!fallback_fonts_resolved_) {
    fallback_fonts_resolved_ = true;
    if (platform_)
      fallback_fonts_ = platform_->GetFallbackFonts();
  }
  if (fallback_fonts_.empty())
    return;
  ImGuiIO& io = ImGui::GetIO();
  // Merging requires a base font to merge into. LoadFont() always leaves one
  // behind, but bail out rather than trip ImGui's assert if that ever changes.
  if (io.Fonts->Fonts.Size == 0)
    return;
  for (const Platform::FontInfo& font : fallback_fonts_) {
    DLOG(0) << "Fallback font: " << font.path << " face " << font.face_index;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontDataOwnedByAtlas = true;
    cfg.FontNo = static_cast<ImU32>(font.face_index);
    // Renders COLRv0 layered glyphs in color.
    cfg.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LoadColor;
    // Color bitmap strikes (Noto Color Emoji, Apple Color Emoji) need a loader
    // of their own; the FreeType loader can neither size nor decode them.
    if (FontFileHasColorBitmapStrikes(font.path, font.face_index))
      cfg.FontLoader = GetColorBitmapFontLoader();
    io.Fonts->AddFontFromFileTTF(font.path.c_str(), kBaseFontSize, &cfg);
  }
}

void ImguiBackend::Shutdown() {
  if (renderer_) {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
      auto id = (Renderer::ResourceId)(intptr_t)tex->TexID;
      if (id != Renderer::kInvalidId)
        renderer_->DestroyTexture(id);
    }
  }
  ImGui::GetIO().BackendPlatformUserData = nullptr;
  ImGui::DestroyContext();
  for (auto id : geometries_)
    if (id != Renderer::kInvalidId)
      renderer_->DestroyGeometry(id);
  geometries_.clear();
  if (shader_ != Renderer::kInvalidId) {
    renderer_->DestroyShader(shader_);
    shader_ = Renderer::kInvalidId;
  }
}

void ImguiBackend::CreateRenderResources(Renderer* renderer) {
  renderer_ = renderer;

  // Invalidate cached geometry so it gets re-uploaded to the new renderer.
  geometries_.clear();
  geometry_hash_ = 0;

  // Create the shader.
  auto source = std::make_unique<ShaderSource>();
  if (source->LoadFromMem("imgui", kImguiVertexShader,
                          sizeof(kImguiVertexShader) - 1, kImguiFragmentShader,
                          sizeof(kImguiFragmentShader) - 1)) {
    shader_ = renderer_->CreateShader(std::move(source), vertex_description_,
                                      kPrimitive_Triangles, false);
  } else {
    DLOG(0) << "Could not create imgui shader.";
  }

  // Recreate textures for context loss (e.g. Vulkan -> OpenGL fallback).
  for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
    if (!tex->Pixels)
      continue;
    Renderer::ResourceId id = renderer_->CreateTexture();
    renderer_->UpdateTexture(id, tex->Width, tex->Height, ImageFormat::kRGBA32,
                             tex->GetSizeInBytes(), (uint8_t*)tex->GetPixels());
    tex->SetTexID((ImTextureID)(intptr_t)id);
  }
}

std::pair<bool, bool> ImguiBackend::ProcessInput(Platform* platform) {
  ImGuiIO& io = ImGui::GetIO();

  // Mouse position first so hover detection is up-to-date.
  // When the cursor is outside the window, send an invalid position so ImGui
  // clears any hover state (e.g. highlighted table rows).
  if (platform->IsCursorInside())
    io.AddMousePosEvent(platform->GetMouseX(), platform->GetMouseY());
  else
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

  // Key events before mouse buttons. ImGui's event trickle defers key state
  // changes that arrive after a mouse button change in the same frame. Sending
  // keys first ensures modifier state (Shift, Ctrl, etc.) is available when
  // the click is processed.
  for (size_t i = 0; i < static_cast<size_t>(Key::MaxKeys); ++i) {
    auto imgui_key = TranslateKey(static_cast<Key>(i));
    bool is_down = platform->IsKeyDown(static_cast<Key>(i));
    io.AddKeyEvent(imgui_key, is_down);
  }

  // ImGui requires modifier flags in addition to physical key events.
  io.AddKeyEvent(ImGuiMod_Ctrl, platform->IsKeyDown(Key::ControlLeft) ||
                                    platform->IsKeyDown(Key::ControlRight));
  io.AddKeyEvent(ImGuiMod_Shift, platform->IsKeyDown(Key::ShiftLeft) ||
                                     platform->IsKeyDown(Key::ShiftRight));
  io.AddKeyEvent(ImGuiMod_Alt, platform->IsKeyDown(Key::AltLeft) ||
                                   platform->IsKeyDown(Key::AltRight));
  io.AddKeyEvent(ImGuiMod_Super, platform->IsKeyDown(Key::SuperLeft) ||
                                     platform->IsKeyDown(Key::SuperRight));

  for (const auto& [button, pressed] : platform->GetMouseButtonEvents()) {
    int imgui_button = -1;
    if (button == MouseButton::Left)
      imgui_button = ImGuiMouseButton_Left;
    else if (button == MouseButton::Right)
      imgui_button = ImGuiMouseButton_Right;
    else if (button == MouseButton::Middle)
      imgui_button = ImGuiMouseButton_Middle;
    if (imgui_button != -1)
      io.AddMouseButtonEvent(imgui_button, pressed);
  }
  // ImGui scrolls 5 * font size vertically and 2 * font size horizontally per
  // unit of wheel input, so 0.6 makes one wheel notch scroll three text lines
  // down and about three characters across. Holding Alt scrolls five times as
  // far per notch. Scroll distance is linear in the wheel value on both the
  // ImGui path and the diff viewer's own wheel handling, so scaling the input
  // here is all that is needed; Alt is otherwise unused by ImGui and by Gel.
  float wheel_scale =
      (platform->IsKeyDown(Key::AltLeft) || platform->IsKeyDown(Key::AltRight))
          ? 0.6f * 5.0f
          : 0.6f;
  io.AddMouseWheelEvent(platform->GetMouseScrollXDelta() * wheel_scale,
                        platform->GetMouseScrollYDelta() * wheel_scale);

  // Character input
  const auto& chars = platform->GetInputCharacters();
  for (unsigned int c : chars) {
    io.AddInputCharacter(c);
  }

  return std::make_pair(io.WantCaptureMouse, io.WantCaptureKeyboard);
}

// Check if an InputText field has selected text and update the primary
// selection (middle-click paste on Linux). Called at the start of each frame
// to capture selection state from the previous frame's widget evaluation.
void ImguiBackend::UpdatePrimarySelection() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  auto& state = g.InputTextState;
  if (state.ID != 0 && state.HasSelection()) {
    int sel_start = state.GetSelectionStart();
    int sel_end = state.GetSelectionEnd();
    if (state.ID != prev_sel_input_id_ || sel_start != prev_sel_start_ ||
        sel_end != prev_sel_end_) {
      prev_sel_input_id_ = state.ID;
      prev_sel_start_ = sel_start;
      prev_sel_end_ = sel_end;
      int start = std::min(sel_start, sel_end);
      int end = std::min(std::max(sel_start, sel_end), state.TextLen);
      start = std::min(start, state.TextLen);
      if (end > start) {
        std::string selected(state.TextA.Data + start, end - start);
        if (!selected.empty())
          platform_->SetPrimarySelection(selected.c_str());
      }
    }
  } else if (prev_sel_input_id_ != 0) {
    prev_sel_input_id_ = 0;
    prev_sel_start_ = 0;
    prev_sel_end_ = 0;
  }
}

void ImguiBackend::NewFrame(float delta_time) {
  UpdatePrimarySelection();

  ImGuiIO& io = ImGui::GetIO();
  int window_w = platform_->GetWindowWidth();
  int window_h = platform_->GetWindowHeight();
  io.DisplaySize = ImVec2((float)window_w, (float)window_h);
  io.DisplayFramebufferScale = ImVec2(
      window_w > 0 ? (float)renderer_->GetFramebufferWidth() / window_w : 1.0f,
      window_h > 0 ? (float)renderer_->GetFramebufferHeight() / window_h
                   : 1.0f);
  io.DeltaTime = delta_time;

  // Apply the cursor that ImGui requested during the previous frame, before
  // NewFrame() resets it back to Arrow.
  platform_->SetMouseCursor(ImGui::GetMouseCursor());

  ImGui::NewFrame();
}

void ImguiBackend::UpdateGeometries() {
  ImDrawData* draw_data = ImGui::GetDrawData();

  // Hash all geometry data to detect changes.
  size_t hash = draw_data->CmdListsCount;
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    hash = base::HashBytes(cmd_list->VtxBuffer.Data,
                           cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), hash);
    hash = base::HashBytes(cmd_list->IdxBuffer.Data,
                           cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), hash);
  }
  if (hash == geometry_hash_)
    return;
  geometry_hash_ = hash;
  if (on_geometry_changed_)
    on_geometry_changed_();

  // Create a geometry for each draw list and upload the vertex data.
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    if ((int)geometries_.size() <= n)
      geometries_.push_back(Renderer::kInvalidId);
    if (geometries_[n] == Renderer::kInvalidId)
      geometries_[n] = renderer_->CreateGeometry(
          kPrimitive_Triangles, vertex_description_, kDataType_UShort);
    renderer_->UpdateGeometry(
        geometries_[n], cmd_list->VtxBuffer.Size, cmd_list->VtxBuffer.Data,
        cmd_list->IdxBuffer.Size, cmd_list->IdxBuffer.Data);
  }
}

void ImguiBackend::UpdateTexture(ImTextureData* tex) {
  switch (tex->Status) {
    case ImTextureStatus_WantCreate: {
      Renderer::ResourceId id = renderer_->CreateTexture();
      renderer_->UpdateTexture(id, tex->Width, tex->Height,
                               ImageFormat::kRGBA32, tex->GetSizeInBytes(),
                               (uint8_t*)tex->GetPixels());
      tex->SetTexID((ImTextureID)(intptr_t)id);
      tex->SetStatus(ImTextureStatus_OK);
      break;
    }
    case ImTextureStatus_WantUpdates: {
      auto id = (Renderer::ResourceId)(intptr_t)tex->TexID;
      auto& r = tex->UpdateRect;
      renderer_->UpdateTextureSubRegion(id, r.x, r.y, r.w, r.h,
                                        ImageFormat::kRGBA32, tex->GetPitch(),
                                        (uint8_t*)tex->GetPixelsAt(r.x, r.y));
      tex->SetStatus(ImTextureStatus_OK);
      break;
    }
    case ImTextureStatus_WantDestroy: {
      if (tex->UnusedFrames > 0) {
        auto id = (Renderer::ResourceId)(intptr_t)tex->TexID;
        renderer_->DestroyTexture(id);
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

void ImguiBackend::Draw() {
  ImGui::Render();
  ImDrawData* draw_data = ImGui::GetDrawData();
  if (!draw_data || draw_data->CmdListsCount <= 0)
    return;

  ImVec2 fb_scale = draw_data->FramebufferScale;
  int fb_width = (int)(draw_data->DisplaySize.x * fb_scale.x);
  int fb_height = (int)(draw_data->DisplaySize.y * fb_scale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  if (draw_data->Textures)
    for (ImTextureData* tex : *draw_data->Textures)
      if (tex->Status != ImTextureStatus_OK)
        UpdateTexture(tex);

  UpdateGeometries();

  renderer_->SetViewport(0, 0, fb_width, fb_height);

  base::Matrix4f proj;
  proj.CreateOrthographicProjection(
      draw_data->DisplayPos.x,
      draw_data->DisplayPos.x + draw_data->DisplaySize.x,
      draw_data->DisplayPos.y + draw_data->DisplaySize.y,
      draw_data->DisplayPos.y);
  renderer_->ActivateShader(shader_);
  renderer_->SetUniform(shader_, "projection", proj);
  renderer_->SetUniform(shader_, "texture_0", 0);

  ImVec2 clip_off = draw_data->DisplayPos;

  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

      // Project scissor rect into framebuffer space and clamp to viewport.
      ImVec2 clip_min(
          std::max((pcmd->ClipRect.x - clip_off.x) * fb_scale.x, 0.0f),
          std::max((pcmd->ClipRect.y - clip_off.y) * fb_scale.y, 0.0f));
      ImVec2 clip_max(std::min((pcmd->ClipRect.z - clip_off.x) * fb_scale.x,
                               (float)fb_width),
                      std::min((pcmd->ClipRect.w - clip_off.y) * fb_scale.y,
                               (float)fb_height));
      if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
        continue;

      renderer_->ActivateTexture(
          (Renderer::ResourceId)(intptr_t)pcmd->GetTexID(), 0);
      renderer_->SetScissor((int)clip_min.x, (int)clip_min.y,
                            (int)(clip_max.x - clip_min.x),
                            (int)(clip_max.y - clip_min.y));
      renderer_->Draw(geometries_[n], pcmd->ElemCount, pcmd->IdxOffset);
    }
  }
  renderer_->ResetScissor();
  renderer_->ResetViewport();
}

}  // namespace eng
