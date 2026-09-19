// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_ICONS_H
#define GEL_UI_ICONS_H

// Draw a circular-arrow refresh icon into the last item's rect.
// Call immediately after the button/item whose rect should contain the icon.
void DrawRefreshIcon();

// Draw a gear/cog settings icon into the last item's rect.
void DrawSettingsIcon();

// Draw a question-mark help icon into the last item's rect.
void DrawHelpIcon();

// Draw a case-sensitivity "Aa" icon into the last item's rect.
void DrawCaseSensitiveIcon();

// Draw a whole-word icon into the last item's rect: a word-shaped slug
// between two boundary ticks.
void DrawWholeWordIcon();

// Draw a small up/down triangle into the last item's rect, for navigation
// buttons. Draw list calls ignore ImGui's disabled alpha, so pass |enabled|
// false to draw in the disabled text color.
void DrawArrowUpIcon(bool enabled);
void DrawArrowDownIcon(bool enabled);

// Draw a funnel with an "X" overlay for removing a path filter.
void DrawClearFilterIcon();

// Draw a panel-toggle icon (two horizontal panels with an arrow).
void DrawDiffPanelIcon();
void DrawSizePanelIcon();

// Chart style icons for the size panel chart view switcher.
void DrawDonutChartIcon();
void DrawBarChartIcon();
void DrawTreemapIcon();

// Sort icons for the size panel file tree.
void DrawSortByNameIcon();
void DrawSortBySizeIcon();

// Collapse-all icon for tree views.
void DrawCollapseAllIcon();

// Folder icons for the directory browser.
void DrawFolderIcon();
void DrawGitFolderIcon();
void DrawGitFirstIcon();

// Simplified app icon for the toolbar.
void DrawAppIcon();

// Window control icons for client-side decorations (CSD).
void DrawMinimizeIcon();
void DrawMaximizeIcon();
void DrawRestoreIcon();
void DrawCloseIcon();

#endif  // GEL_UI_ICONS_H
