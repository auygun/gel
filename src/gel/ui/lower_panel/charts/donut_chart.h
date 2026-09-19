// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_CHARTS_DONUT_CHART_H
#define GEL_UI_LOWER_PANEL_CHARTS_DONUT_CHART_H

#include "gel/ui/lower_panel/charts/chart_renderer.h"

class DonutChart : public ChartRenderer {
 public:
  explicit DonutChart(Delegate& delegate) : ChartRenderer(delegate) {}

  void Render(const std::vector<SliceInfo>& slices) override;
};

#endif  // GEL_UI_LOWER_PANEL_CHARTS_DONUT_CHART_H
