// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_COMMIT_SIZE_H
#define GEL_UI_LOWER_PANEL_COMMIT_SIZE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gel/commands/git_cat_file.h"
#include "gel/commands/git_diff_tree.h"
#include "gel/ui/lower_panel/charts/chart_renderer.h"
#include "gel/ui/lower_panel/size_file_tree.h"

class BarChart;
class DonutChart;
class PersistentSettings;
class TreemapChart;

// Lower panel: file tree with sizes on the left, pie chart on the right.
// The tree shows all files touched by the selected commit. Clicking a
// directory in the tree updates the pie chart to show that directory's
// breakdown.
class CommitSize : public ChartRenderer::Delegate,
                   public SizeFileTree::Delegate {
 public:
  CommitSize(PersistentSettings& settings,
             std::function<void(bool)> busy_callback);
  ~CommitSize();

  void Update();

  void SetCommit(const std::string& commit_hash);

 private:
  // ChartRenderer::Delegate implementation.
  int GetChildCount() const override;
  int64_t GetTotalSize() const override;
  const std::string& GetChildName(int child_index) const override;
  int64_t GetChildSize(int child_index) const override;
  bool IsChildDirectory(int child_index) const override;
  void DrillDown(int child_index) override;
  void NavigateToParent() override;

  // SizeFileTree::Delegate implementation.
  void OnDirectorySelected(SizeFileTree::TreeNode* node) override;

  void RenderChartStyleButtons();
  void RenderBreadcrumb();
  std::vector<SliceInfo> ComputeSlices();

  PersistentSettings& settings_;

  GitDiffTree git_diff_tree_;
  GitCatFile git_cat_file_;

  SizeFileTree file_tree_;

  std::string commit_;

  // Intermediate state: diff-tree entries waiting for cat-file sizes.
  std::vector<DiffTreeEntry> pending_entries_;

  std::unique_ptr<DonutChart> donut_chart_;
  std::unique_ptr<BarChart> bar_chart_;
  std::unique_ptr<TreemapChart> treemap_chart_;

  bool chart_tooltip_suppressed_ = false;
};

#endif  // GEL_UI_LOWER_PANEL_COMMIT_SIZE_H
