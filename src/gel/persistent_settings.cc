// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/persistent_settings.h"

#include <algorithm>
#include <fstream>

#include "gel/ui/style.h"
#include "third_party/json/json.h"
#include "third_party/kaliber/base/log.h"
#include "third_party/kaliber/platform/platform.h"

const char* GetDiffColorModeName(DiffColorMode mode) {
  switch (mode) {
    case DiffColorMode::kSyntax:
      return "Syntax highlighting";
    case DiffColorMode::kAnsi:
      return "ANSI colors";
    case DiffColorMode::kAnsiWithBackground:
      return "ANSI colors with background";
    case DiffColorMode::kCount:
      break;
  }
  return "ANSI colors";
}

const char* GetColumnDataName(ColumnData data) {
  switch (data) {
    case ColumnData::kAuthor:
      return "Author";
    case ColumnData::kAuthorDate:
      return "Author date";
    case ColumnData::kCommit:
      return "Commit hash";
    case ColumnData::kCommitter:
      return "Committer";
    case ColumnData::kCommitterDate:
      return "Committer date";
    case ColumnData::kCount:
      break;
  }
  return "";
}

const char* GetChartStyleName(ChartStyle chart_style) {
  switch (chart_style) {
    case ChartStyle::kDonut:
      return "Donut";
    case ChartStyle::kBar:
      return "Bar";
    case ChartStyle::kTreemap:
      return "Treemap";
    case ChartStyle::kCount:
      break;
  }
  return "Donut";
}

const char* GetSizeTreeSortName(SizeTreeSort sort) {
  switch (sort) {
    case SizeTreeSort::kSize:
      return "Size";
    case SizeTreeSort::kName:
      return "Name";
    case SizeTreeSort::kCount:
      break;
  }
  return "Size";
}

const char* GetDisplayBackendName(DisplayBackend backend) {
  switch (backend) {
    case DisplayBackend::kAuto:
      return "Auto";
    case DisplayBackend::kWayland:
      return "Wayland";
    case DisplayBackend::kX11:
      return "X11";
    case DisplayBackend::kCount:
      break;
  }
  return "Auto";
}

const char* GetRendererTypeName(eng::RendererType type) {
  switch (type) {
    case eng::RendererType::kVulkan:
      return "Vulkan";
    case eng::RendererType::kOpenGL:
      return "OpenGL";
    case eng::RendererType::kUnknown:
      break;
  }
  return "Vulkan";
}

namespace {

template <typename T, typename NameFunc>
T ParseEnum(const std::string& str, T max, NameFunc name_func, T fallback) {
  for (int i = 0; i < static_cast<int>(max); ++i) {
    T val = static_cast<T>(i);
    if (str == name_func(val))
      return val;
  }
  return fallback;
}

}  // namespace

void PersistentSettings::Load() {
  auto path = eng::Platform::GetSettingsPath();
  if (path.empty()) {
    LOG(0) << "Could not determine settings path";
    return;
  }

  std::ifstream file(path);
  if (!file)
    return;

  nlohmann::json j;
  try {
    file >> j;
  } catch (const std::exception& e) {
    LOG(0) << "Failed to parse " << path << ": " << e.what();
    return;
  }

  if (j.contains("font_scale") && j["font_scale"].is_number())
    font_scale = j["font_scale"].get<float>();
  if (j.contains("diff_tool") && j["diff_tool"].is_string())
    snprintf(diff_tool, sizeof(diff_tool), "%s",
             j["diff_tool"].get<std::string>().c_str());
  if (j.contains("font_name") && j["font_name"].is_string())
    snprintf(font_name, sizeof(font_name), "%s",
             j["font_name"].get<std::string>().c_str());
  if (j.contains("font_path") && j["font_path"].is_string())
    snprintf(font_path, sizeof(font_path), "%s",
             j["font_path"].get<std::string>().c_str());
  if (j.contains("vulkan_device") && j["vulkan_device"].is_string())
    snprintf(vulkan_device, sizeof(vulkan_device), "%s",
             j["vulkan_device"].get<std::string>().c_str());
  if (j.contains("font_face_index") && j["font_face_index"].is_number_integer())
    font_face_index = std::max(0, j["font_face_index"].get<int>());
  if (j.contains("style")) {
    if (j["style"].is_string())
      style = ParseEnum(j["style"].get<std::string>(), Style::kCount,
                        GetStyleName, Style::kSystem);
    else if (j["style"].is_number_integer())
      style = static_cast<Style>(j["style"].get<int>());
  }
  if (j.contains("layout")) {
    if (j["layout"].is_string())
      layout = ParseEnum(j["layout"].get<std::string>(), Layout::kCount,
                         GetLayoutName, Layout::kDefault);
    else if (j["layout"].is_number_integer())
      layout = static_cast<Layout>(j["layout"].get<int>());
  }
  if (j.contains("window_x") && j["window_x"].is_number_integer())
    window_x = j["window_x"].get<int>();
  if (j.contains("window_y") && j["window_y"].is_number_integer())
    window_y = j["window_y"].get<int>();
  if (j.contains("window_width") && j["window_width"].is_number_integer())
    window_width = j["window_width"].get<int>();
  if (j.contains("window_height") && j["window_height"].is_number_integer())
    window_height = j["window_height"].get<int>();
  if (j.contains("window_maximized") && j["window_maximized"].is_boolean())
    window_maximized = j["window_maximized"].get<bool>();
  if (j.contains("console_window_x") &&
      j["console_window_x"].is_number_integer())
    console_window_x = j["console_window_x"].get<int>();
  if (j.contains("console_window_y") &&
      j["console_window_y"].is_number_integer())
    console_window_y = j["console_window_y"].get<int>();
  if (j.contains("console_window_width") &&
      j["console_window_width"].is_number_integer())
    console_window_width = j["console_window_width"].get<int>();
  if (j.contains("console_window_height") &&
      j["console_window_height"].is_number_integer())
    console_window_height = j["console_window_height"].get<int>();
  if (j.contains("console_window_open") &&
      j["console_window_open"].is_boolean())
    console_window_open = j["console_window_open"].get<bool>();
  if (j.contains("console_window_collapsed") &&
      j["console_window_collapsed"].is_boolean())
    console_window_collapsed = j["console_window_collapsed"].get<bool>();
  if (j.contains("upper_panel_height") && j["upper_panel_height"].is_number())
    upper_panel_height = j["upper_panel_height"].get<float>();
  if (j.contains("file_list_width") && j["file_list_width"].is_number())
    file_list_width = j["file_list_width"].get<float>();
  if (j.contains("size_tree_width") && j["size_tree_width"].is_number())
    size_tree_width = j["size_tree_width"].get<float>();

  if (j.contains("table_columns") && j["table_columns"].is_array()) {
    table_columns.clear();
    for (auto& v : j["table_columns"]) {
      if (v.is_string()) {
        auto str = v.get<std::string>();
        auto col = ParseEnum(str, ColumnData::kCount, GetColumnDataName,
                             ColumnData::kCount);
        if (col != ColumnData::kCount)
          table_columns.push_back(col);
      } else if (v.is_number_integer()) {
        int val = std::clamp(v.get<int>(), 0,
                             static_cast<int>(ColumnData::kCount) - 1);
        table_columns.push_back(static_cast<ColumnData>(val));
      }
    }
  }

  if (j.contains("table_column_widths") &&
      j["table_column_widths"].is_array()) {
    table_column_widths.clear();
    for (auto& v : j["table_column_widths"]) {
      if (v.is_number())
        table_column_widths.push_back(std::max(0.0f, v.get<float>()));
    }
  }

  if (j.contains("file_list_on_right") && j["file_list_on_right"].is_boolean())
    file_list_on_right = j["file_list_on_right"].get<bool>();
  if (j.contains("renderer")) {
    if (j["renderer"].is_string()) {
      auto str = j["renderer"].get<std::string>();
      if (str == GetRendererTypeName(eng::RendererType::kOpenGL))
        renderer_type = eng::RendererType::kOpenGL;
      else
        renderer_type = eng::RendererType::kVulkan;
    } else if (j["renderer"].is_number_integer()) {
      renderer_type = static_cast<eng::RendererType>(j["renderer"].get<int>());
    }
  }
  if (j.contains("context_lines") && j["context_lines"].is_number_integer())
    context_lines = j["context_lines"].get<int>();
  if (j.contains("display_backend")) {
    if (j["display_backend"].is_string())
      display_backend = ParseEnum(j["display_backend"].get<std::string>(),
                                  DisplayBackend::kCount, GetDisplayBackendName,
                                  DisplayBackend::kAuto);
    else if (j["display_backend"].is_number_integer())
      display_backend =
          static_cast<DisplayBackend>(j["display_backend"].get<int>());
  }
#if defined(OS_LINUX)
  if (j.contains("client_side_decorations") &&
      j["client_side_decorations"].is_boolean())
    client_side_decorations = j["client_side_decorations"].get<bool>();
#endif
  if (j.contains("search_case_sensitive") &&
      j["search_case_sensitive"].is_boolean())
    search_options.case_sensitive = j["search_case_sensitive"].get<bool>();
  if (j.contains("search_whole_word") && j["search_whole_word"].is_boolean())
    search_options.whole_word = j["search_whole_word"].get<bool>();
  if (j.contains("diff_search_case_sensitive") &&
      j["diff_search_case_sensitive"].is_boolean())
    diff_search_options.case_sensitive =
        j["diff_search_case_sensitive"].get<bool>();
  if (j.contains("diff_search_whole_word") &&
      j["diff_search_whole_word"].is_boolean())
    diff_search_options.whole_word = j["diff_search_whole_word"].get<bool>();
  if (j.contains("show_line_numbers") && j["show_line_numbers"].is_boolean())
    show_line_numbers = j["show_line_numbers"].get<bool>();
  if (j.contains("diff_color_mode")) {
    if (j["diff_color_mode"].is_string())
      diff_color_mode = ParseEnum(j["diff_color_mode"].get<std::string>(),
                                  DiffColorMode::kCount, GetDiffColorModeName,
                                  DiffColorMode::kAnsi);
    else if (j["diff_color_mode"].is_number_integer())
      diff_color_mode =
          static_cast<DiffColorMode>(j["diff_color_mode"].get<int>());
  } else if (j.contains("syntax_highlighting") &&
             j["syntax_highlighting"].is_boolean()) {
    diff_color_mode = j["syntax_highlighting"].get<bool>()
                          ? DiffColorMode::kSyntax
                          : DiffColorMode::kAnsi;
  }
  // show_size_panel is intentionally not loaded; always start in diff view.
  if (j.contains("chart_style")) {
    if (j["chart_style"].is_string())
      chart_style =
          ParseEnum(j["chart_style"].get<std::string>(), ChartStyle::kCount,
                    GetChartStyleName, ChartStyle::kDonut);
    else if (j["chart_style"].is_number_integer())
      chart_style = static_cast<ChartStyle>(j["chart_style"].get<int>());
  }
  if (j.contains("size_tree_sort")) {
    if (j["size_tree_sort"].is_string())
      size_tree_sort = ParseEnum(j["size_tree_sort"].get<std::string>(),
                                 SizeTreeSort::kCount, GetSizeTreeSortName,
                                 SizeTreeSort::kSize);
    else if (j["size_tree_sort"].is_number_integer())
      size_tree_sort =
          static_cast<SizeTreeSort>(j["size_tree_sort"].get<int>());
  }

  if (j.contains("ext_mappings") && j["ext_mappings"].is_object()) {
    ext_mappings.clear();
    for (auto& [key, val] : j["ext_mappings"].items()) {
      if (val.is_string())
        ext_mappings.emplace_back(key, val.get<std::string>());
    }
  }

  // Sanitize loaded values.
  font_scale = std::clamp(font_scale, 0.5f, 2.0f);
  style = static_cast<Style>(std::clamp(static_cast<int>(style), 0,
                                        static_cast<int>(Style::kCount) - 1));
  layout = static_cast<Layout>(std::clamp(
      static_cast<int>(layout), 0, static_cast<int>(Layout::kCount) - 1));
  renderer_type = static_cast<eng::RendererType>(
      std::clamp(static_cast<int>(renderer_type), 1, 2));
  display_backend = static_cast<DisplayBackend>(
      std::clamp(static_cast<int>(display_backend), 0,
                 static_cast<int>(DisplayBackend::kCount) - 1));
#if defined(OS_APPLE)
  if (renderer_type == eng::RendererType::kOpenGL)
    renderer_type = eng::RendererType::kVulkan;
#endif
  if (window_width != -1)
    window_width = std::clamp(window_width, 320, 7680);
  if (window_height != -1)
    window_height = std::clamp(window_height, 240, 4320);
  context_lines = std::max(context_lines, 0);
  chart_style = static_cast<ChartStyle>(
      std::clamp(static_cast<int>(chart_style), 0,
                 static_cast<int>(ChartStyle::kCount) - 1));
  size_tree_sort = static_cast<SizeTreeSort>(
      std::clamp(static_cast<int>(size_tree_sort), 0,
                 static_cast<int>(SizeTreeSort::kCount) - 1));
  diff_color_mode = static_cast<DiffColorMode>(
      std::clamp(static_cast<int>(diff_color_mode), 0,
                 static_cast<int>(DiffColorMode::kCount) - 1));
}

void PersistentSettings::Save() {
  auto path = eng::Platform::GetSettingsPath();
  if (path.empty()) {
    LOG(0) << "Could not determine settings path";
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    LOG(0) << "Failed to create " << path.parent_path() << ": " << ec.message();
    return;
  }

  nlohmann::json j;
  j["font_scale"] = font_scale;
  j["diff_tool"] = diff_tool;
  j["font_name"] = font_name;
  j["font_path"] = font_path;
  j["font_face_index"] = font_face_index;
  j["style"] = GetStyleName(style);
  j["layout"] = GetLayoutName(layout);
  j["renderer"] = GetRendererTypeName(renderer_type);
  j["vulkan_device"] = vulkan_device;
  j["window_x"] = window_x;
  j["window_y"] = window_y;
  j["window_width"] = window_width;
  j["window_height"] = window_height;
  j["window_maximized"] = window_maximized;
  j["console_window_x"] = console_window_x;
  j["console_window_y"] = console_window_y;
  j["console_window_width"] = console_window_width;
  j["console_window_height"] = console_window_height;
  j["console_window_open"] = console_window_open;
  j["console_window_collapsed"] = console_window_collapsed;
  j["context_lines"] = context_lines;
  j["upper_panel_height"] = upper_panel_height;
  j["file_list_width"] = file_list_width;
  j["size_tree_width"] = size_tree_width;
  j["file_list_on_right"] = file_list_on_right;
  j["display_backend"] = GetDisplayBackendName(display_backend);
  j["client_side_decorations"] = client_side_decorations;
  j["search_case_sensitive"] = search_options.case_sensitive;
  j["search_whole_word"] = search_options.whole_word;
  j["diff_search_case_sensitive"] = diff_search_options.case_sensitive;
  j["diff_search_whole_word"] = diff_search_options.whole_word;
  j["show_line_numbers"] = show_line_numbers;
  j["diff_color_mode"] = GetDiffColorModeName(diff_color_mode);
  // show_size_panel is intentionally not saved; always start in diff view.
  j["chart_style"] = GetChartStyleName(chart_style);
  j["size_tree_sort"] = GetSizeTreeSortName(size_tree_sort);

  nlohmann::json cols = nlohmann::json::array();
  for (auto c : table_columns)
    cols.push_back(GetColumnDataName(c));
  j["table_columns"] = cols;

  nlohmann::json widths = nlohmann::json::array();
  for (auto w : table_column_widths)
    widths.push_back(w);
  j["table_column_widths"] = widths;

  if (!ext_mappings.empty()) {
    nlohmann::json mappings = nlohmann::json::object();
    for (const auto& [ext, lang] : ext_mappings)
      mappings[ext] = lang;
    j["ext_mappings"] = mappings;
  }

  std::ofstream file(path);
  if (!file) {
    LOG(0) << "Failed to open " << path << " for writing";
    return;
  }

  file << j.dump(2) << "\n";
}

void PersistentSettings::SanitizeLayoutValues(int width, int height) {
  if (width <= 0 || height <= 0)
    return;
  upper_panel_height =
      std::clamp(upper_panel_height, 50.0f, (float)height - 55.0f);
  file_list_width = std::clamp(file_list_width, 50.0f, (float)width - 55.0f);
  size_tree_width = std::clamp(size_tree_width, 50.0f, (float)width - 55.0f);
}
