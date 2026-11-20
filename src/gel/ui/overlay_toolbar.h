// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_OVERLAY_TOOLBAR_H
#define GEL_UI_OVERLAY_TOOLBAR_H

struct ImVec2;

// A small toolbar floating over the top-right corner of a panel: the diff
// view's controls, and the size view's tree and chart controls. They all share
// its geometry and look, so they read as the same kind of control.
//
// Lay out the content on one line between Begin() and End(), separated by
// ImGui::SameLine(). Buttons are ButtonSize() square, except the stacked
// half-height ones, which are HalfButtonWidth() wide.
class OverlayToolbar {
 public:
  static float ButtonSize();
  static float HalfButtonWidth();
  // Between items, and between the toolbar and the panel's edges.
  static float Spacing();
  // Width of a Separator(), not counting the spacing around it.
  static float SeparatorWidth();
  // Distance from the anchor down to the toolbar's bottom edge, for keeping
  // content clear of it.
  static float BottomOffset();
  // Distance from the anchor down to the toolbar's vertical center, for
  // aligning content beside it.
  static float CenterOffset();

  // The top-right corner of the current window, inset by the scrollbar width
  // whether or not the window has one, as the diff view's text viewer does, so
  // that the toolbars line up across panels. To pass to Begin() after the
  // window ends.
  static ImVec2 WindowAnchor();

  // The toolbar's width for |content_width| of content.
  static float Width(float content_width);

  // Begins the toolbar, with |content_width| of content, right-aligned below
  // |anchor|, the top-right corner of the panel's content in screen
  // coordinates. Call it in the window containing the panel, after the
  // panel's own child window, so that the toolbar is drawn over the panel and
  // gets the mouse first, and doesn't add to the panel's scrollable area.
  void Begin(const char* id, const ImVec2& anchor, float content_width);
  void End();

  // Shows a delayed tooltip for the last item, held back after a click until
  // the mouse leaves the toolbar.
  void Tooltip(const char* text);

  // A vertical line between groups of items, like the main toolbar's. Call it
  // between ImGui::SameLine() calls.
  void Separator();

  // Expand all and Collapse all, as two stacked half-height buttons marked
  // with the down and right arrows of open and closed directories. Sets
  // |expand| or |collapse| when clicked.
  void ExpandCollapseButtons(bool& expand, bool& collapse);

 private:
  bool tooltip_suppressed_ = false;
  bool any_hovered_ = false;
};

#endif  // GEL_UI_OVERLAY_TOOLBAR_H
