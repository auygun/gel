// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_CHARTS_CHART_RENDERER_H
#define GEL_UI_LOWER_PANEL_CHARTS_CHART_RENDERER_H

#include <cstdint>
#include <string>
#include <vector>

struct SliceInfo {
  int child_index;  // -1 for the "Others" bucket.
  float fraction;
  int64_t size = 0;
  int others_count = 0;
};

// Utility functions for chart colors and formatting.
std::string HumanSize(int64_t bytes);
uint32_t SliceColor(int index);
uint32_t SliceColorDark(int index);
uint32_t SliceColorLight(int index);
uint32_t LerpColor(uint32_t a, uint32_t b, float t);

class ChartRenderer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual int GetChildCount() const = 0;
    virtual int64_t GetTotalSize() const = 0;
    virtual const std::string& GetChildName(int child_index) const = 0;
    virtual int64_t GetChildSize(int child_index) const = 0;
    virtual bool IsChildDirectory(int child_index) const = 0;
    virtual void DrillDown(int child_index) = 0;
    virtual void NavigateToParent() = 0;
  };

  explicit ChartRenderer(Delegate& delegate) : delegate_(delegate) {}
  virtual ~ChartRenderer() = default;

  virtual void Render(const std::vector<SliceInfo>& slices) = 0;

 protected:
  Delegate& delegate_;
};

#endif  // GEL_UI_LOWER_PANEL_CHARTS_CHART_RENDERER_H
