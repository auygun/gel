// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_CHARTS_TREEMAP_CHART_H
#define GEL_UI_LOWER_PANEL_CHARTS_TREEMAP_CHART_H

#include "gel/ui/lower_panel/charts/chart_renderer.h"

class TreemapChart : public ChartRenderer {
 public:
  explicit TreemapChart(Delegate& delegate) : ChartRenderer(delegate) {}

  void Render(const std::vector<SliceInfo>& slices) override;
};

#endif  // GEL_UI_LOWER_PANEL_CHARTS_TREEMAP_CHART_H
