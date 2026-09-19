// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_SETTINGS_MODAL_H
#define GEL_UI_SETTINGS_MODAL_H

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gel/persistent_settings.h"
#include "gel/ui/style.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/renderer/renderer.h"

struct SettingsModalResult {
  std::optional<std::tuple<Style, Layout, float>> pending_style;
  std::optional<eng::RendererType> pending_renderer;
  std::optional<PendingFont> pending_font;
};

// Modal dialog for editing application settings. Manages its own editing state
// and style override so the modal is not affected by live style preview.
class SettingsModal {
 public:
  SettingsModal();

  // Snapshots current settings into editing state and opens the popup on the
  // next Update() call.
  void Open(PersistentSettings& settings,
            eng::RendererType current_renderer,
            const std::vector<eng::Platform::FontInfo>& fonts,
            std::vector<std::string> gpu_names,
            int selected_gpu);

  // Renders the modal. Returns signals for pending style or renderer changes.
  SettingsModalResult Update(PersistentSettings& settings,
                             ImTextureID icon_texture,
                             bool window_focused);

  // Returns true while the modal is open (style override is active).
  bool IsOpen() const {
    return settings_modal_style_override_.has_value() ||
           recapture_style_override_;
  }

 private:
  // Recomputes filtered_fonts_ from fonts_ and font_filter_.
  void RebuildFontFilter();

  bool show_settings_ = false;
  float settings_font_scale_ = 1;
  std::optional<ImGuiStyle> settings_modal_style_override_;
  bool recapture_style_override_ = false;
  char settings_diff_tool_[128] = {};
  Style settings_style_ = Style::kSystem;
  Layout settings_layout_ = Layout::kDefault;
  eng::RendererType settings_renderer_type_ = eng::RendererType::kVulkan;
  eng::RendererType current_renderer_ = eng::RendererType::kVulkan;
  DiffColorMode settings_diff_color_mode_ = DiffColorMode::kAnsi;
  bool settings_file_list_on_right_ = false;
  bool settings_show_line_numbers_ = true;
  bool settings_csd_ = false;
  DisplayBackend settings_display_backend_ = DisplayBackend::kAuto;
  int settings_font_index_ = 0;
  int original_font_index_ = 0;
  const std::vector<eng::Platform::FontInfo>* fonts_ = nullptr;
  // Indices into *fonts_ matching font_filter_, monospace group first. Rebuilt
  // only when the filter text changes rather than every frame the combo is
  // open, since the list runs to several hundred families.
  std::vector<int> filtered_fonts_;
  size_t filtered_mono_count_ = 0;
  char font_filter_[64] = "";
  bool scroll_to_selected_font_ = false;
  std::vector<std::string> gpu_names_;
  int settings_gpu_index_ = 0;
  int original_gpu_index_ = 0;
  std::vector<ColumnData> settings_table_columns_;
  std::vector<std::pair<std::string, std::string>> settings_ext_mappings_;
  char new_ext_input_[32] = {};
  int new_ext_lang_index_ = 1;
  bool had_nav_focus_ = false;
  bool had_sub_popup_ = false;
  bool desktop_entry_installed_ = false;
  bool bash_completion_installed_ = false;
#if defined(OS_LINUX)
  void InstallBashCompletion();
  void UninstallBashCompletion();
#endif
  std::string git_version_;
};

#endif  // GEL_UI_SETTINGS_MODAL_H
