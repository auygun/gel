// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_PERSISTENT_SETTINGS_H
#define GEL_PERSISTENT_SETTINGS_H

#include <string>
#include <utility>
#include <vector>

#include "base/text_search.h"
#include "gel/ui/style.h"
#include "third_party/kaliber/renderer/renderer.h"

enum class ChartStyle { kDonut, kBar, kTreemap, kCount };

enum class SizeTreeSort { kSize, kName, kCount };

enum class DisplayBackend { kAuto, kWayland, kX11, kCount };

enum class DiffColorMode { kSyntax, kAnsi, kAnsiWithBackground, kCount };

enum class ColumnData {
  kAuthor,
  kAuthorDate,
  kCommit,
  kCommitter,
  kCommitterDate,
  kCount
};

const char* GetDiffColorModeName(DiffColorMode mode);
const char* GetColumnDataName(ColumnData data);
const char* GetChartStyleName(ChartStyle chart_style);
const char* GetSizeTreeSortName(SizeTreeSort sort);
const char* GetDisplayBackendName(DisplayBackend backend);
const char* GetRendererTypeName(eng::RendererType type);

// A font selection in flight: the atlas is rebuilt between frames, so the
// choice travels from the settings modal to Gel rather than being applied on
// the spot. An empty path means the built-in default font.
struct PendingFont {
  std::string path;
  int face_index = 0;
};

class PersistentSettings {
 public:
  float font_scale = 1;
  char diff_tool[128] = "";
  char font_name[256] = "";
  char font_path[512] = "";
  int font_face_index = 0;
  Style style = Style::kSystem;
  Layout layout = Layout::kDefault;
  eng::RendererType renderer_type = eng::RendererType::kVulkan;
  char vulkan_device[256] = "";
  DisplayBackend display_backend = DisplayBackend::kAuto;

  int window_x = -1;
  int window_y = -1;
  int window_width = -1;
  int window_height = -1;
  bool window_maximized = false;

  int console_window_x = -1;
  int console_window_y = -1;
  int console_window_width = -1;
  int console_window_height = -1;
  bool console_window_open = false;
  bool console_window_collapsed = false;

  int context_lines = 3;
  float upper_panel_height = 300.0f;
  float file_list_width = 200.0f;
  float size_tree_width = 250.0f;
  bool file_list_on_right = false;
  bool client_side_decorations = false;
  base::MatchOptions search_options;
  base::MatchOptions diff_search_options;
  bool show_line_numbers = true;
  DiffColorMode diff_color_mode = DiffColorMode::kSyntax;
  bool show_size_panel = false;
  ChartStyle chart_style = ChartStyle::kDonut;
  SizeTreeSort size_tree_sort = SizeTreeSort::kSize;
  std::vector<ColumnData> table_columns = {ColumnData::kAuthor,
                                           ColumnData::kAuthorDate};
  std::vector<float> table_column_widths;
  // Custom file extension -> language name mappings for syntax highlighting.
  std::vector<std::pair<std::string, std::string>> ext_mappings;

  void Load();
  void Save();

  void SanitizeLayoutValues(int width, int height);
};

#endif  // GEL_PERSISTENT_SETTINGS_H
