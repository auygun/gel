// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/settings_modal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "base/exec.h"
#include "gel/ui/syntax_highlight.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/base/task_runner.h"
#include "third_party/kaliber/base/thread_pool.h"
#include "version.h"
#if defined(OS_LINUX)
#include "gel/ui/completion_data.h"
#include "icon_data.h"
#endif

using namespace eng;

namespace {

PendingFont ToPendingFont(const eng::Platform::FontInfo& font) {
  return PendingFont{font.path, font.face_index};
}

// The commit history table supports at most this many configurable columns in
// addition to the graph/subject column.
constexpr size_t kMaxTableColumns = 6;

}  // namespace

SettingsModal::SettingsModal() = default;

void SettingsModal::Open(PersistentSettings& settings,
                         RendererType current_renderer,
                         const std::vector<Platform::FontInfo>& fonts,
                         std::vector<std::string> gpu_names,
                         int selected_gpu) {
  settings_font_scale_ = settings.font_scale;
  snprintf(settings_diff_tool_, sizeof(settings_diff_tool_), "%s",
           settings.diff_tool);
  settings_style_ = settings.style;
  settings_layout_ = settings.layout;
  settings_renderer_type_ = current_renderer;
  current_renderer_ = current_renderer;
  settings_diff_color_mode_ = settings.diff_color_mode;
  settings_table_columns_ = settings.table_columns;
  settings_ext_mappings_ = settings.ext_mappings;
  new_ext_input_[0] = '\0';
  new_ext_lang_index_ = 1;
  settings_file_list_on_right_ = settings.file_list_on_right;
  settings_show_line_numbers_ = settings.show_line_numbers;
  settings_csd_ = settings.client_side_decorations;
  settings_display_backend_ = settings.display_backend;
  fonts_ = &fonts;
  settings_font_index_ = 0;
  if (settings.font_name[0] != '\0') {
    for (size_t i = 0; i < fonts.size(); i++) {
      if (fonts[i].name == settings.font_name) {
        settings_font_index_ = static_cast<int>(i) + 1;
        break;
      }
    }
  }
  original_font_index_ = settings_font_index_;
  gpu_names_ = std::move(gpu_names);
  settings_gpu_index_ = selected_gpu;
  original_gpu_index_ = selected_gpu;
  had_nav_focus_ = false;
  show_settings_ = true;

  if (git_version_.empty()) {
    base::ThreadPool::Get().PostTaskAndReplyWithResult<std::string>(
        HERE,
        []() -> std::string {
          Exec exec;
          if (!exec.Start({"git", "--version"}))
            return {};
          while (exec.Poll())
            ;
          std::string& line = exec.GetOut();
          if (auto nl = line.find('\n'); nl != std::string::npos)
            line.resize(nl);
          while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
          if (!line.empty()) {
            constexpr std::string_view prefix = "git version ";
            if (line.starts_with(prefix))
              line = line.substr(prefix.size());
            return std::move(line);
          }
          return {};
        },
        [this](std::string version) { git_version_ = std::move(version); });
  }
}

void SettingsModal::RebuildFontFilter() {
  filtered_fonts_.clear();
  filtered_mono_count_ = 0;
  if (!fonts_)
    return;
  // Case-insensitive substring match on the family name.
  std::string needle = font_filter_;
  std::transform(needle.begin(), needle.end(), needle.begin(), [](char c) {
    return static_cast<char>(tolower(static_cast<unsigned char>(c)));
  });
  auto matches = [&](const std::string& name) {
    if (needle.empty())
      return true;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
      return static_cast<char>(tolower(static_cast<unsigned char>(c)));
    });
    return lower.find(needle) != std::string::npos;
  };
  // Two passes so the monospace group leads and each group stays name-sorted.
  for (bool mono : {true, false}) {
    for (size_t i = 0; i < fonts_->size(); i++) {
      const auto& font = (*fonts_)[i];
      if (font.is_monospace == mono && matches(font.name))
        filtered_fonts_.push_back(static_cast<int>(i));
    }
    if (mono)
      filtered_mono_count_ = filtered_fonts_.size();
  }
}

SettingsModalResult SettingsModal::Update(PersistentSettings& settings,
                                          ImTextureID icon_texture,
                                          bool window_focused) {
  SettingsModalResult result;

  if (show_settings_) {
    settings_modal_style_override_ = ImGui::GetStyle();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::OpenPopup("Settings");
    show_settings_ = false;
  }

  // Re-capture the override after a style change has been applied.
  if (recapture_style_override_) {
    settings_modal_style_override_ = ImGui::GetStyle();
    recapture_style_override_ = false;
  }

  // Swap in the saved style so the modal is not affected by live preview.
  ImGuiStyle active_style;
  if (settings_modal_style_override_) {
    active_style = ImGui::GetStyle();
    ImGui::GetStyle() = *settings_modal_style_override_;
  }
  // Reverts the live style and font preview. Shared by Cancel, Escape and the
  // title bar's close button.
  auto revert_preview = [&] {
    result.pending_style =
        std::make_tuple(settings.style, settings.layout, settings.font_scale);
    if (settings_font_index_ != original_font_index_) {
      if (original_font_index_ == 0)
        result.pending_font = PendingFont{};
      else
        result.pending_font = ToPendingFont(
            (*fonts_)[static_cast<size_t>(original_font_index_) - 1]);
    }
    settings_modal_style_override_.reset();
  };

  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
  bool was_open = ImGui::IsPopupOpen("Settings");
  bool stay_open = true;
  if (ImGui::BeginPopupModal(
          "Settings", &stay_open,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
    // Capture before widgets process input so that Escape used to
    // deactivate an InputText or combo doesn't also trigger Cancel.
    // Also skip when the nav cursor is visible (a widget was tabbed to) so
    // Escape goes to the focused widget instead of Cancel.
    bool no_item_active = !ImGui::IsAnyItemActive() &&
                          !(ImGui::GetCurrentContext()->NavCursorVisible &&
                            ImGui::GetCurrentContext()->NavId != 0);
    // A combo dropdown was open on the previous frame.  NewFrame() already
    // closed it in response to Escape, so we can't detect it this frame.
    bool had_sub_popup = had_sub_popup_;
    // Track whether a widget had nav focus on the previous frame.  Escape
    // should only close the modal when nothing was focused; if a widget was
    // focused, the first Escape just unfocuses it (NavUpdateCancelRequest
    // clears NavId before we run, so we can't tell from the current frame).
    bool nav_was_focused = had_nav_focus_;
    // After Escape clears nav focus (NavId=0), forward Tab no longer
    // initializes focus automatically.  Detect this and explicitly request
    // focus on the next widget so Tab->Escape->Tab keeps working.
    if (ImGui::GetCurrentContext()->NavId == 0 && !ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
      ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::BeginCombo("Style", GetStyleName(settings_style_))) {
      for (int i = 0; i < static_cast<int>(Style::kCount); i++) {
        bool selected = (settings_style_ == static_cast<Style>(i));
        if (ImGui::Selectable(GetStyleName(static_cast<Style>(i)), selected)) {
          settings_style_ = static_cast<Style>(i);

          result.pending_style = std::make_tuple(
              static_cast<Style>(i), settings_layout_, settings_font_scale_);
          settings_modal_style_override_.reset();
          recapture_style_override_ = true;
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Layout", GetLayoutName(settings_layout_))) {
      for (int i = 0; i < static_cast<int>(Layout::kCount); i++) {
        bool selected = (settings_layout_ == static_cast<Layout>(i));
        if (ImGui::Selectable(GetLayoutName(static_cast<Layout>(i)),
                              selected)) {
          settings_layout_ = static_cast<Layout>(i);

          result.pending_style = std::make_tuple(
              settings_style_, static_cast<Layout>(i), settings_font_scale_);
          settings_modal_style_override_.reset();
          recapture_style_override_ = true;
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (fonts_ && !fonts_->empty()) {
      const char* font_label =
          settings_font_index_ == 0
              ? "Default"
              : (*fonts_)[settings_font_index_ - 1].name.c_str();
      // Without constraints of our own BeginCombo caps the popup at eight
      // items and gives it a scrollbar, which would sit alongside the list's.
      // Supplying them skips that cap so the popup auto-fits the search box
      // plus the list, and only the list scrolls. The minimum width is raised
      // to the combo's own width by BeginComboPopup.
      ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                          ImVec2(FLT_MAX, FLT_MAX));
      if (ImGui::BeginCombo("Font", font_label)) {
        if (ImGui::IsWindowAppearing()) {
          font_filter_[0] = '\0';
          RebuildFontFilter();
          ImGui::SetKeyboardFocusHere();
          // SetItemDefaultFocus() does not scroll the list child, so a font
          // far down the list would open off screen. Scroll to it explicitly.
          scroll_to_selected_font_ = true;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##font_filter", "Search fonts",
                                     font_filter_, sizeof(font_filter_)))
          RebuildFontFilter();

        // Cap the list rather than letting it grow to hundreds of rows, but
        // shrink to fit when a filter narrows it down.
        float row_height = ImGui::GetTextLineHeightWithSpacing();
        size_t rows = filtered_fonts_.size() + 1;  // + the "Default" entry
        if (filtered_mono_count_ > 0 &&
            filtered_mono_count_ < filtered_fonts_.size())
          rows++;  // separator
        float list_height = std::min<size_t>(rows, 12) * row_height;
        if (ImGui::BeginChild("##font_list", ImVec2(0, list_height),
                              ImGuiChildFlags_None)) {
          // Emits one Selectable per visible row of [begin, end) in
          // filtered_fonts_, clipped so a long list costs only what is shown.
          auto draw_rows = [&](size_t begin, size_t end) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(end - begin), row_height);
            for (size_t i = begin; i < end; i++) {
              if (settings_font_index_ == filtered_fonts_[i] + 1)
                clipper.IncludeItemByIndex(static_cast<int>(i - begin));
            }
            while (clipper.Step()) {
              for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                   row++) {
                size_t i = begin + static_cast<size_t>(row);
                const auto& font = (*fonts_)[filtered_fonts_[i]];
                int idx = filtered_fonts_[i] + 1;
                bool selected = (settings_font_index_ == idx);
                ImGui::PushID(idx);
                if (ImGui::Selectable(font.name.c_str(), selected)) {
                  if (settings_font_index_ != idx) {
                    settings_font_index_ = idx;
                    result.pending_font = ToPendingFont(font);
                  }
                }
                if (selected) {
                  ImGui::SetItemDefaultFocus();
                  // The clipper submits the selected row even when it is out
                  // of view, so its position here is the real one.
                  if (scroll_to_selected_font_)
                    ImGui::SetScrollHereY(0.5f);
                }
                ImGui::PopID();
              }
            }
          };

          bool default_selected = (settings_font_index_ == 0);
          if (ImGui::Selectable("Default", default_selected)) {
            if (settings_font_index_ != 0) {
              settings_font_index_ = 0;
              result.pending_font = PendingFont{};
            }
          }
          if (default_selected)
            ImGui::SetItemDefaultFocus();

          // Monospace first: the UI measures text rather than assuming a cell
          // width, so proportional fonts work, but code and diffs read better
          // in a fixed pitch and the default should stay easy to find.
          draw_rows(0, filtered_mono_count_);
          if (filtered_mono_count_ > 0 &&
              filtered_mono_count_ < filtered_fonts_.size())
            ImGui::Separator();
          draw_rows(filtered_mono_count_, filtered_fonts_.size());
          scroll_to_selected_font_ = false;

          if (filtered_fonts_.empty() && font_filter_[0] != '\0')
            ImGui::TextDisabled("No match");
        }
        ImGui::EndChild();
        ImGui::EndCombo();
      }
    }
    if (ImGui::SliderFloat("Font scale", &settings_font_scale_, 0.5f, 2.0f,
                           "%.02f")) {
      result.pending_style = std::make_tuple(settings_style_, settings_layout_,
                                             settings_font_scale_);
    }
    ImGui::Separator();
#if defined(OS_LINUX)
    {
      const char* backend_names[] = {"Auto", "Wayland", "X11"};
      int backend_index = static_cast<int>(settings_display_backend_);
      if (ImGui::BeginCombo("Display", backend_names[backend_index])) {
        for (int i = 0; i < static_cast<int>(DisplayBackend::kCount); i++) {
          bool selected = (backend_index == i);
          if (ImGui::Selectable(backend_names[i], selected))
            settings_display_backend_ = static_cast<DisplayBackend>(i);
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (settings_display_backend_ != settings.display_backend)
        ImGui::TextDisabled("Requires restart");
    }
#endif
    {
#if !defined(OS_APPLE)
      const char* renderer_names[] = {"Vulkan", "OpenGL"};
      int renderer_index =
          settings_renderer_type_ == RendererType::kOpenGL ? 1 : 0;
      if (ImGui::BeginCombo("Renderer", renderer_names[renderer_index])) {
        for (int i = 0; i < 2; i++) {
          bool selected = (renderer_index == i);
          if (ImGui::Selectable(renderer_names[i], selected))
            settings_renderer_type_ =
                i == 0 ? RendererType::kVulkan : RendererType::kOpenGL;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
#endif
    }
    if (settings_renderer_type_ == RendererType::kVulkan &&
        gpu_names_.size() > 1) {
      const char* gpu_label = gpu_names_[settings_gpu_index_].c_str();
      if (ImGui::BeginCombo("GPU", gpu_label)) {
        for (int i = 0; i < static_cast<int>(gpu_names_.size()); i++) {
          bool selected = (settings_gpu_index_ == i);
          if (ImGui::Selectable(gpu_names_[i].c_str(), selected))
            settings_gpu_index_ = i;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
#if defined(OS_LINUX)
    ImGui::Checkbox("Client-side decorations", &settings_csd_);
    if (settings_csd_ != settings.client_side_decorations)
      ImGui::TextDisabled("Requires restart");
    ImGui::Separator();
#endif
    if (ImGui::TreeNode("Commit list columns")) {
      int remove_idx = -1;
      for (size_t i = 0; i < settings_table_columns_.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        // The first table column holds the graph and subject, so the
        // configurable ones start at column 2.
        ImGui::Text("Column %d", static_cast<int>(i) + 2);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::BeginCombo("##data",
                              GetColumnDataName(settings_table_columns_[i]))) {
          for (int d = 0; d < static_cast<int>(ColumnData::kCount); d++) {
            auto data = static_cast<ColumnData>(d);
            bool selected = (settings_table_columns_[i] == data);
            if (ImGui::Selectable(GetColumnDataName(data), selected))
              settings_table_columns_[i] = data;
            if (selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
          remove_idx = static_cast<int>(i);
        ImGui::PopID();
      }
      if (remove_idx >= 0)
        settings_table_columns_.erase(settings_table_columns_.begin() +
                                      remove_idx);
      ImGui::BeginDisabled(settings_table_columns_.size() >= kMaxTableColumns);
      if (ImGui::SmallButton("Add"))
        settings_table_columns_.push_back(ColumnData::kAuthor);
      ImGui::EndDisabled();
      ImGui::TreePop();
    }
    ImGui::InputTextWithHint("Diff tool", "Default", settings_diff_tool_,
                             sizeof(settings_diff_tool_));
    {
      const char* file_list_names[] = {"Left", "Right"};
      int file_list_index = settings_file_list_on_right_ ? 1 : 0;
      if (ImGui::BeginCombo("File list", file_list_names[file_list_index])) {
        for (int i = 0; i < 2; i++) {
          bool selected = (file_list_index == i);
          if (ImGui::Selectable(file_list_names[i], selected))
            settings_file_list_on_right_ = (i == 1);
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
    if (ImGui::BeginCombo("Diff colors",
                          GetDiffColorModeName(settings_diff_color_mode_))) {
      for (int i = 0; i < static_cast<int>(DiffColorMode::kCount); i++) {
        bool selected =
            (settings_diff_color_mode_ == static_cast<DiffColorMode>(i));
        if (ImGui::Selectable(
                GetDiffColorModeName(static_cast<DiffColorMode>(i)), selected))
          settings_diff_color_mode_ = static_cast<DiffColorMode>(i);
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (ImGui::TreeNode("File extensions")) {
      int remove_idx = -1;
      for (size_t i = 0; i < settings_ext_mappings_.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(settings_ext_mappings_[i].first.c_str());
        ImGui::SameLine(0, ImGui::GetFontSize());
        ImGui::TextDisabled("%s", settings_ext_mappings_[i].second.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
          remove_idx = static_cast<int>(i);
        ImGui::PopID();
      }
      if (remove_idx >= 0)
        settings_ext_mappings_.erase(settings_ext_mappings_.begin() +
                                     remove_idx);
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
      ImGui::InputTextWithHint("##ext", "ext", new_ext_input_,
                               sizeof(new_ext_input_));
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
      Language preview_lang = static_cast<Language>(new_ext_lang_index_);
      if (ImGui::BeginCombo("##lang", GetLanguageName(preview_lang))) {
        for (int i = 0; i <= kLanguageCount; i++) {
          Language lang = static_cast<Language>(i);
          bool selected = (new_ext_lang_index_ == i);
          if (ImGui::Selectable(GetLanguageName(lang), selected))
            new_ext_lang_index_ = i;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Add") && new_ext_input_[0] != '\0') {
        std::string ext = new_ext_input_;
        // Remove leading dot if present.
        if (!ext.empty() && ext[0] == '.')
          ext = ext.substr(1);
        if (!ext.empty()) {
          // Lowercase.
          for (char& c : ext)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
          // Remove existing mapping for this extension.
          std::erase_if(settings_ext_mappings_,
                        [&](const auto& p) { return p.first == ext; });
          settings_ext_mappings_.emplace_back(
              ext, GetLanguageName(static_cast<Language>(new_ext_lang_index_)));
          new_ext_input_[0] = '\0';
        }
      }
      ImGui::TreePop();
    }
    ImGui::Checkbox("Show line numbers", &settings_show_line_numbers_);
#if defined(OS_LINUX)
    ImGui::Separator();
    if (ImGui::Button("(Re)Install")) {
      eng::InstallDesktopEntry(kIconData);
      desktop_entry_installed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Uninstall")) {
      eng::UninstallDesktopEntry();
      desktop_entry_installed_ = false;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(desktop_entry_installed_
                               ? "desktop icon \xE2\x9C\x93"
                               : "desktop icon");
    {
      ImGui::PushID("bash_completion");
      if (ImGui::Button("(Re)Install")) {
        InstallBashCompletion();
        bash_completion_installed_ = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Uninstall")) {
        UninstallBashCompletion();
        bash_completion_installed_ = false;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(bash_completion_installed_
                                 ? "bash completion \xE2\x9C\x93"
                                 : "bash completion");
      ImGui::PopID();
    }
#endif
    ImGui::Separator();
    if (icon_texture) {
      float icon_size = ImGui::GetTextLineHeight() * 6;
      ImGui::Image(icon_texture, ImVec2(icon_size, icon_size));
      ImGui::SameLine();
      float text_height = ImGui::GetTextLineHeight();
      float spacing = ImGui::GetStyle().ItemSpacing.y;
      float block_height = text_height * 2 + spacing;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + icon_size - block_height);
      ImGui::BeginGroup();
      ImGui::TextDisabled("gel %s", GEL_VERSION);
      ImGui::TextDisabled("git %s",
                          !git_version_.empty() ? git_version_.c_str() : "...");
      ImGui::EndGroup();
    } else {
      ImGui::TextDisabled("gel %s", GEL_VERSION);
      ImGui::TextDisabled("git %s",
                          !git_version_.empty() ? git_version_.c_str() : "...");
    }
    ImGui::Separator();
    float button_width = ImGui::GetFontSize() * 6;
    if (ImGui::Button("Save", ImVec2(button_width, 0))) {
      settings.style = settings_style_;
      settings.layout = settings_layout_;
      settings.font_scale = settings_font_scale_;
      settings.renderer_type = settings_renderer_type_;
      snprintf(settings.diff_tool, sizeof(settings.diff_tool), "%s",
               settings_diff_tool_);
      settings.diff_color_mode = settings_diff_color_mode_;
      settings.show_line_numbers = settings_show_line_numbers_;
      settings.table_columns = settings_table_columns_;
      settings.ext_mappings = settings_ext_mappings_;
      settings.file_list_on_right = settings_file_list_on_right_;
#if defined(OS_LINUX)
      settings.client_side_decorations = settings_csd_;
#endif
      settings.display_backend = settings_display_backend_;
      if (fonts_) {
        if (settings_font_index_ == 0) {
          settings.font_name[0] = '\0';
          settings.font_path[0] = '\0';
          settings.font_face_index = 0;
        } else {
          const auto& fi = (*fonts_)[settings_font_index_ - 1];
          snprintf(settings.font_name, sizeof(settings.font_name), "%s",
                   fi.name.c_str());
          snprintf(settings.font_path, sizeof(settings.font_path), "%s",
                   fi.path.c_str());
          settings.font_face_index = fi.face_index;
        }
      }
      if (settings_gpu_index_ < static_cast<int>(gpu_names_.size()))
        snprintf(settings.vulkan_device, sizeof(settings.vulkan_device), "%s",
                 gpu_names_[settings_gpu_index_].c_str());
      settings.Save();
      if (settings_renderer_type_ != current_renderer_ ||
          settings_gpu_index_ != original_gpu_index_)
        result.pending_renderer = settings_renderer_type_;
      if (settings_modal_style_override_) {
        ImGui::GetStyle() = active_style;
        settings_modal_style_override_.reset();
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(button_width, 0)) ||
        (no_item_active && !nav_was_focused && !had_sub_popup &&
         ImGui::IsKeyPressed(ImGuiKey_Escape))) {
      revert_preview();
      ImGui::CloseCurrentPopup();
    }
    had_nav_focus_ = ImGui::GetCurrentContext()->NavCursorVisible &&
                     ImGui::GetCurrentContext()->NavId != 0;
    had_sub_popup_ = ImGui::GetCurrentContext()->OpenPopupStack.Size >
                     ImGui::GetCurrentContext()->BeginPopupStack.Size;
    ImGui::EndPopup();
  }
  // The title bar's close button dismisses the popup before the body runs, so
  // it has to take the Cancel path from out here. Must happen before the
  // live-preview style is restored below, like the in-body path does.
  if (was_open && !stay_open)
    revert_preview();
  if (!window_focused)
    ImGui::PopStyleColor();
  // Restore the active (live-preview) style.
  if (settings_modal_style_override_)
    ImGui::GetStyle() = active_style;

  return result;
}

#if defined(OS_LINUX)
void SettingsModal::InstallBashCompletion() {
  namespace fs = std::filesystem;

  const char* home = std::getenv("HOME");
  if (!home)
    return;

  fs::path dir =
      fs::path(home) / ".local" / "share" / "bash-completion" / "completions";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    DLOG(0) << "Failed to create " << dir << ": " << ec.message();
    return;
  }

  fs::path path = dir / "gel";
  std::ofstream file(path);
  if (!file) {
    DLOG(0) << "Failed to write " << path;
    return;
  }
  file.write(kBashCompletionData, sizeof(kBashCompletionData) - 1);
  DLOG(0) << "Installed " << path;
}

void SettingsModal::UninstallBashCompletion() {
  namespace fs = std::filesystem;

  const char* home = std::getenv("HOME");
  if (!home)
    return;

  fs::path path = fs::path(home) / ".local" / "share" / "bash-completion" /
                  "completions" / "gel";
  std::error_code ec;
  if (fs::remove(path, ec))
    DLOG(0) << "Removed " << path;
  else if (ec)
    DLOG(0) << "Failed to remove " << path << ": " << ec.message();
}
#endif
