// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_LOWER_PANEL_H
#define GEL_UI_LOWER_PANEL_LOWER_PANEL_H

class PersistentSettings;

// The lower panel's layout, shared by all its views (diff, size, ...): a side
// pane (the file list, the size tree) and a main pane (the diff, the chart)
// next to each other, split by a draggable vertical separator. The side pane's
// width and which side it is on are the same in every view, so switching
// views keeps the layout.
class LowerPanel {
 public:
  // The views it lays out.
  class View {
   public:
    virtual ~View() = default;

    // Renders the side pane at |width| (-FLT_MIN fills the rest of the line),
    // in a child window of its own.
    virtual void RenderSidePane(float width) = 0;

    // Renders the main pane at |width| (-FLT_MIN fills the rest of the line),
    // in a child window of its own.
    virtual void RenderMainPane(float width) = 0;

    // Called after both panes, still inside the panel: for what floats over
    // them, like the overlay toolbar, and for syncing one pane with the other.
    virtual void FinishFrame() {}
  };

  explicit LowerPanel(PersistentSettings& settings);

  // Lays out |view| in the panel. Call once per frame for the active view.
  void Update(View& view);

 private:
  void RenderSeparator(float available, float thickness);

  PersistentSettings& settings_;
};

#endif  // GEL_UI_LOWER_PANEL_LOWER_PANEL_H
