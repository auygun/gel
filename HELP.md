# Gel Help

Press **Ctrl+F** to search this help, with the same case and whole-word
toggles as the other search bars. **Enter** jumps to the next match,
**Shift+Enter** to the previous. **Tab** / **Shift+Tab** jump to the next /
previous section heading.

Gel is a graphical repository browser for Git. It visualizes commit history and
diffs, and supports operations like cherry-picking, reverting, rebasing, and
merging.

## Table of Contents

- [Usage](#usage)
- [Interface Layout](#interface-layout)
  - [Client-Side Decorations](#client-side-decorations)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Toolbar](#toolbar)
  - [Commit Hash Field](#commit-hash-field)
  - [Clear Path Filter](#clear-path-filter)
  - [Commit Search](#commit-search)
  - [Status Indicators](#status-indicators)
- [Commit History (Upper Panel)](#commit-history-upper-panel)
  - [Graph](#graph)
  - [Keyboard Navigation](#keyboard-navigation)
  - [Unstaged and Staged Changes](#unstaged-and-staged-changes)
    - [Synthetic Row Context Menu](#synthetic-row-context-menu)
  - [Branch and Tag Labels](#branch-and-tag-labels)
    - [Tag Context Menu](#tag-context-menu)
    - [Branch Context Menu](#branch-context-menu)
  - [Right-Click Context Menu](#right-click-context-menu)
- [Console](#console)
- [Diff View (Lower Panel)](#diff-view-lower-panel)
  - [File List](#file-list)
    - [Multi-Selection](#multi-selection)
  - [Diff Viewer](#diff-viewer)
  - [Diff Overlay Controls](#diff-overlay-controls)
- [Size View (Lower Panel)](#size-view-lower-panel)
  - [File Tree](#file-tree-left-side)
  - [Chart View](#chart-view-right-side)
  - [Chart Interaction](#chart-interaction)
- [Directory Browser](#directory-browser)
- [Settings](#settings)
  - [Available Themes](#available-themes)
  - [Linux-Specific Options](#linux-specific-options)
  - [Version Info](#version-info)
- [Persistent Settings](#persistent-settings)
- [Platform Support](#platform-support)

## Usage

```
gel [git-log-options...]
```

Any arguments passed to Gel are forwarded to the underlying `git log` command.
This lets you filter the history by branch, path, author, date range, etc.

**Examples:**

```
gel                          # Show full history of the current branch
gel main                     # Show history of the main branch
gel --author=alice           # Show only commits by alice
gel --since="2024-01-01"     # Show commits since a date
gel -- src/                  # Show commits that touch files under src/
gel feature-branch..main     # Show commits in main but not in feature-branch
gel -n 100                   # Show only the last 100 commits
gel --topo-order             # Sort by topology (no interleaving of branches)
gel --date=short             # Use short date format in log and diff
```

## Interface Layout

The window is divided into two main panels:

- **Upper panel** -- Commit history table with graph visualization
- **Lower panel** -- Diff view (file list + diff viewer) or size view (file
  tree + chart), toggled with F6

Both panel dividers can be dragged to resize. The file list can be placed on
the left (default) or right side via Settings.

All dialog windows (help, settings, console, image diff, commit) are
movable and resizable. Message and error popups support text selection and
**Ctrl+C** to copy.

When closing the application, a spinner is displayed while background tasks
finish. An **Exit now** button is available to terminate immediately without
waiting.

### Client-Side Decorations

Client-side decorations (CSD) are only available on Linux (X11 and Wayland);
the setting is not shown on other platforms.

When CSD is enabled, the native
window title bar is replaced by custom window controls drawn in the toolbar.
The app icon appears at the left edge of the toolbar -- right-click it to
open the system window menu. Minimize, maximize/restore, and close buttons
appear at the right edge. The window title is displayed in the toolbar between
the controls. CSD can be toggled in Settings (requires restart).

## Keyboard Shortcuts

- **F1** -- Open this help window
- **F5** -- Refresh commit history and diff
- **F6** -- Toggle lower panel between diff and size view
- **F7** -- Toggle console
- **Ctrl+,** -- Open settings
- **Ctrl+L** -- Clear path filter
- **Ctrl+F** -- Open/focus diff search bar
- **Ctrl+A** -- Select all diff content
- **Ctrl+C** -- Copy selected text to clipboard
- **Ctrl+T** -- Stage selected file(s) to commit (unstaged view)
- **Ctrl+U** -- Unstage selected file(s) from commit (staged view)
- **Ctrl+J** -- Revert selected file(s) with confirmation (unstaged view)
- **Ctrl+Plus/Minus** -- Increase/decrease font scale
- **Ctrl+0** -- Reset font scale to 1.0
- **Ctrl+Q** -- Quit
- **/** -- Focus the commit search field
- **Tab / Shift+Tab** -- Cycle focus between toolbar input fields
- **Enter / Escape** -- Dismiss message or error popups (text is selectable
  in popups)
- **Alt+Mouse Wheel** -- Scroll five times faster in any scrollable view,
  vertically and horizontally

## Toolbar

The toolbar spans the top of the window and contains the following controls
(left to right):

- **App icon** -- Shown when using CSD; right-click for the window menu
- **Commit hash field** -- Shows/searches commit hashes (see below)
- **Row counter** -- Shows selected row and total count (e.g. 42/1500)
- **Refresh** -- Reloads the commit history and diff (F5)
- **Clear filter** -- Clears all active path filters in a single press (Ctrl+L)
- **Search field** -- Search commits by author, committer, message, tag,
  or branch name
- **Case toggle** -- Toggle case-sensitive / case-insensitive commit search
- **Whole word toggle** -- Match complete words only; letters, digits and `_`
  count as part of a word
- **Search arrows** -- Navigate forward/backward through search matches
- **Panel toggle** -- Switch lower panel between diff and size view (F6)
- **Settings** -- Opens the settings modal (Ctrl+,)
- **Help** -- Opens this help window (F1)
- **Status dot** -- Small dot to the left of the branch name: yellow while a
  git task is in progress, gray otherwise. Click to open the console
  (F7). See [Status Indicators](#status-indicators)
- **Branch / HEAD status** -- Current branch name (or detached HEAD hash),
  right-aligned. Right-click for a branch context menu listing all local
  branches with options to checkout, rebase, merge, reset, rename, create,
  delete, or open in a new window

### Commit Hash Field

The toolbar contains a text field showing the full SHA-1 hash of the selected
commit.

- Text is auto-selected when the field gains focus.
- Type a partial hash and press **Enter** to search for and jump to a matching
  commit.
- Selected text is copied to the X11 primary selection (Linux) for
  middle-click paste.

### Clear Path Filter

When a path filter is active (from command-line arguments), the toolbar
clear filter button clears all filters (diff, status, and log) in a
single press and reloads the full history. The diff overlay also has its
own clear filter button that clears only the diff/status filter without
affecting the log filter.

### Commit Search

Type a search term in the search field to find commits matching by author,
committer, message content, tag name, or branch name (the "stash" label
matches too). By default the search is case-insensitive and matches anywhere
in a field; the two toggle buttons next to the search field switch to
case-sensitive and to whole-word matching. With whole word on, `fix` matches
in `fix-typo`, `fix/foo` and `v1.fix`, but not in `fixup`, `prefix` or
`fix_typo` -- letters, digits and `_` count as part of a word. Matching rows
are displayed in bold in the commit history.

- Press **Enter** or click the down arrow to jump to the next match.
- Press **Shift+Enter** or click the up arrow to jump to the previous match.
- **Shift+Up** / **Shift+Down** also navigate between matches.
- Enter and Shift+Enter work while the search input is focused; the input
  retains focus with text selected after navigation.
- A progress bar appears in the search field while scanning large histories.

### Status Indicators

A small status dot appears in the toolbar to the left of the branch name.
It is yellow while a git task (cherry-pick, revert, rebase, or merge) is in
progress, and gray otherwise. The dot brightens on hover, and a tooltip
shows the task in progress (e.g. "Cherry-pick in progress (F7)") or
"Console (F7)" when idle.

Click the dot or press **F7** to open the [console](#console).

## Commit History (Upper Panel)

A scrollable table showing a commit graph and commit details. The default
columns are Message, Author, and Author Date.

- Click a row to select a commit and view its diff below.
- The first commit is auto-selected on startup.
- Columns are resizable by dragging the column borders.
- Which columns are shown is configurable in [Settings](#settings).

### Graph

The Message column includes a commit graph with colored lanes showing branch
topology, merges, and parallel development lines.

### Keyboard Navigation

- **Up / Down** -- Move selection one commit at a time
- **Page Up/Down** -- Move selection one page at a time
- **Home / End** -- Jump to the first / last commit
- **Ctrl+Up/Down/Page Up/Down/Home/End** -- Scroll without changing selection
- **Left / Right** -- Navigate backward / forward in selection history

Navigating back to a commit also returns its diff to where it was scrolled to
when you left it. Scrolling the diff while it reloads cancels this and leaves
the view where you put it.

### Unstaged and Staged Changes

When the working tree has local modifications, synthetic rows appear at the
top of the commit list:

- **Unstaged changes** -- Shows the diff of uncommitted modifications,
  including untracked files.
- **Staged changes** -- Shows the diff of staged (indexed) changes.

#### Synthetic Row Context Menu

Right-click the **Unstaged changes** row to access:

- **Stage All Files to Commit** -- Stage all files (`git add -A`).
- **Revert All Files** -- Revert all tracked changes and delete untracked
  files. Shows a confirmation dialog.

Right-click the **Staged changes** row to access:

- **Unstage All Files from Commit** -- Unstage all files (`git reset HEAD`).
- **Commit** -- Opens a popup to enter a commit message. The message is
  preserved if the popup is closed and reopened. An **Amend** checkbox
  pre-populates the message from the last commit for amending. An 80-character
  width guide is shown. Right-click the text input for a context menu.

### Branch and Tag Labels

Commits with branches or tags display them as colored labels next to the
commit message. Tags are shown first (alphabetically), followed by branches
(alphabetically). The current branch is rendered in bold with a green
indicator.

When multiple tags would exceed the available space, they collapse into a
single "N tags" label. Right-click it to see each tag listed in a submenu.
A single long tag name is truncated with "..." and the full name appears in
a tooltip.

The tip of the stash stack gets a **stash** label, shown before any tag and
branch labels, when the history includes it (e.g. `gel --all`). The label has
no context menu. Older stash entries are left undecorated by git and appear
as ordinary commits.

#### Tag Context Menu

Right-click a tag label to access:

- **Checkout (detached)** -- Check out the tag in detached HEAD state.
- **Reset** -- Reset the current branch (Soft, Mixed, or Hard).
- **Copy tag name** -- Copy the tag name to the clipboard.
- **Open in new window** -- Open a new Gel instance at that tag.
- **Create/Move tag** -- Create a new tag at the same commit (inline input
  field). If the tag already exists, a confirmation dialog offers to move it.
- **Delete tag** -- Delete the tag.

#### Branch Context Menu

Right-click a branch label to access:

- **Checkout** -- Switch to the branch (disabled for remote branches and
  the current branch).
- **Rebase** -- Rebase the current HEAD onto the branch.
- **Merge** -- Merge the branch (Merge, No-ff, or FF-only).
- **Reset** -- Reset the current branch (Soft, Mixed, or Hard).
- **Copy branch name** -- Copy the branch name to the clipboard.
- **Open in new window** -- Open a new Gel instance at that branch.
- **Rename** -- Rename the branch (inline input field, disabled for remote
  branches).
- **Create branch** -- Create a new branch from this branch (inline input
  field).
- **Delete branch** -- Force-delete the branch (disabled for remote branches
  and the current branch).

### Right-Click Context Menu

Right-click a commit row to access:

- **Revert** -- Create a new commit that undoes the selected commit.
- **Cherry-pick** -- Apply the commit onto the current branch (Commit or
  No-commit).
- **Interactive rebase** -- Start an interactive rebase from the selected
  commit (Autosquash or No-autosquash).
- **Reset** -- Reset the current branch to the selected commit (Soft, Mixed,
  or Hard).
- **Create/Move tag** -- Create a tag at the selected commit (inline input
  field). If the tag already exists, a confirmation dialog offers to move it.
- **Copy commit subject** -- Copy the commit's subject line to the clipboard.
- **Create branch** -- Create a branch at the selected commit (inline input
  field).

## Console

The console shows the output of git commands in real time. Unlike a
modal dialog, the console is non-modal -- you can continue interacting with
the rest of the application while it is open. The window supports text
selection, search (Ctrl+F), and a right-click context menu. While a task is
in progress, the window title shows the task name (e.g. "Console -
Cherry-pick").

The console does not open automatically when a task is detected (for
example, one started in another terminal). Open it by clicking the
[status dot](#status-indicators) in the toolbar or pressing **F7**. It does
open automatically when a git command fails, and a previously open console
is restored at startup.

The window can be collapsed by clicking the collapse button to save space
while a task runs. Press **F7** or click the status dot in the toolbar to
toggle the window open/closed.

When a task is waiting for user input (e.g. due to conflicts), action
buttons appear at the bottom:

- **Continue** -- Proceed with the task after resolving conflicts.
- **Skip** -- Skip the current step (shown for cherry-pick, revert, and
  rebase; not for merge).
- **Abort** -- Cancel the task and roll back to the state before it
  started.
- **Quit** -- Discard the task state without rolling back changes
  (`git <task> --quit`); the console reports the task as cancelled.
- **Run git-gui** -- Launch `git gui citool` to help resolve conflicts.

A **Close** button is always available to dismiss the window. When no task
is in progress, a **Clear** button also appears, which clears the rendered
console history. The window's position, size, and collapsed/open state are
saved and restored across sessions.

## Diff View (Lower Panel)

The diff view contains a file list sidebar and a diff viewer showing the
full colored diff for the selected commit. The commit header at the top
includes the commit hash, parent commit hashes (clickable -- **Ctrl+Click**
to navigate to a parent commit), author and committer (each with date on
the same line), and the full commit message.

### File List

Lists the files changed in the selected commit, preceded by a colored status
letter:

- **M** (Yellow) -- Modified
- **A** (Green) -- Added
- **D** (Red) -- Deleted
- **R** (Blue) -- Renamed
- **C** (Cyan) -- Copied
- **S** (Magenta) -- Submodule

A **Commit** entry at the top scrolls to the commit message in the diff.
Click a file to scroll the diff to that file's section.
Scrolling through the diff auto-selects the corresponding file. Long file
paths show a tooltip on hover.

#### Multi-Selection

When viewing unstaged or staged changes, multi-selection is available:

- **Ctrl+click** -- Toggle individual file selection.
- **Shift+click** -- Select a range of files.
- **Box select** -- Click and drag to select files.
- **Escape** -- Clear the selection.

#### Right-Click Context Menu

Right-click a file entry to access:

- **External diff** -- Runs `git difftool` to open the file in an external
  diff tool, comparing the parent commit to the selected commit. Also works
  for unstaged changes, staged changes, and root commits. The tool can be
  configured in Settings. Disabled for multi-file selections.
- **Visual diff** -- For image files, opens a non-modal side-by-side
  comparison showing the old and new versions of the image with dimensions.
  Multiple image diff windows can be open simultaneously. Shows "(no image)"
  for the missing side of added or deleted files. Also works for unstaged and
  staged changes. Disabled for multi-file selections.
- **Copy path** -- Copies the file's relative path to the clipboard. When
  multiple files are selected, copies all paths joined by spaces.

Additional items appear for unstaged and staged changes:

- **Select All** -- Select all files (shown when multi-selection is active).
- **Stage to Commit** (Ctrl+T) -- Stage the selected file(s) for unstaged
  files.
- **Revert Change(s)** (Ctrl+J) -- Revert the selected file(s) with a
  confirmation dialog. Also handles untracked files. For unstaged files only.
- **Unstage from Commit** (Ctrl+U) -- Unstage the selected file(s) for
  staged files.

### Diff Viewer

Features:

- Line numbers displayed alongside diff content (toggleable in Settings).
  Line numbers are hidden for combined diffs.
- Color-coded additions (green) and deletions (red).
- Optional syntax highlighting for diff content (configurable in Settings).
- File header lines (diff --git ...) have a distinct background color.
- Merge conflict regions in local changes are highlighted with distinct
  background colors for current and incoming changes.
- Rename/copy detection, merge commit combined diffs, submodule diffs, and
  textconv support.
- Horizontal scrollbar for long lines. Horizontal mouse/trackpad scrolling is
  also supported.

#### External Links

URLs (`http://` and `https://`) in commit messages are automatically detected
and rendered as underlined links. **Ctrl+Click** a link to open it in the
default browser. Right-click a link for a context menu with **Open link in
browser** and **Copy link URL**.

#### Diff Search

Press **Ctrl+F** to open a search bar within the diff viewer.

- Type a term and press **Enter** to jump to the next match.
- **Shift+Enter** jumps to the previous match.
- Arrow buttons next to the search field navigate between matches.
- A case toggle button switches between case-sensitive and case-insensitive
  matching.
- A whole-word toggle button restricts matches to complete words.
- **Escape** closes the search bar.
- Matches are highlighted and the view scrolls horizontally to center them.

#### Right-Click Context Menu

Right-click in the diff viewer to access:

- **Select all** -- Select all diff content.
- **Copy** -- Copy selected text to the clipboard.
- **Show origin of this line** -- Run `git blame` to find the commit that
  originally introduced the clicked line. If the originating commit is in
  the loaded history, it is selected and scrolled to, with the origin line
  highlighted. Selecting a different commit or opening a new context menu
  cancels any in-progress blame search.
- **Run git gui blame on this line** -- Launch `git gui blame` for the
  file at the clicked line number.

Both line-origin menu items are only available for lines that exist in the
repository (not for added lines in unstaged/staged changes).

A spinning indicator appears near the mouse cursor while background
operations are running (blame lookups, git commands, commit search, or file
size fetching).

#### Text Selection

- **Click + drag** -- Select text character by character
- **Double-click** -- Select the word under the cursor
- **Double-click + drag** -- Extend selection word by word
- **Shift + click** -- Extend selection to click position
- **Ctrl+A** -- Select all diff content
- **Ctrl+C** -- Copy selected text to clipboard

Dragging above/below the visible area auto-scrolls, with speed increasing
based on distance. On Linux, mouse-selected text is automatically placed in
the X11 primary selection for middle-click paste (Ctrl+A does not update it).

### Diff Overlay Controls

A floating overlay in the upper-right corner of the diff content area provides
diff-specific controls:

- **Refresh** -- Refresh the diff only (without reloading the commit history).
- **Clear filter** -- Clear the diff/status path filter without affecting the
  log filter.
- **Context lines** -- Number field with +/- buttons to adjust diff context
  lines (0-99999). The diff is reloaded, but the view stays on the code it was
  showing: the line at the top of the viewport is kept there, and the file list
  selection follows. Scrolling while the diff reloads cancels this and leaves
  the view where you put it.

The overlay appears on hover and has a semi-transparent background.

## Size View (Lower Panel)

Press **F6** or click the panel toggle button to switch the lower panel from
the diff view to the size view. The size view shows the files touched by the
selected commit, organized in a file tree with sizes, alongside an interactive
chart.

### File Tree (Left Side)

A hierarchical tree of directories and files changed in the selected commit,
with sizes displayed next to each entry.

- Click a directory to select it and update the chart.
- Three buttons at the top control the tree:
  - **Collapse all** -- Collapse all expanded directories.
  - **Sort by name** -- Sort entries alphabetically.
  - **Sort by size** -- Sort entries by size (largest first, default).

### Chart View (Right Side)

Visualizes the size breakdown of the selected directory. A breadcrumb path at
the top shows the current directory with clickable components for navigation.
The / button jumps to the root. Total directory size is shown in parentheses.

Three chart styles are available via buttons in the top-right corner:

- **Donut chart** -- Pie-like ring visualization (default).
- **Bar chart** -- Horizontal bars showing file sizes.
- **Treemap** -- Rectangular area-proportional layout.

### Chart Interaction

- **Click** a slice, bar, or region to drill down into that directory.
- **Right-click** the donut chart to navigate to the parent directory.
- Click a breadcrumb component to jump directly to that directory.

## Directory Browser

When started from a directory that is not inside a Git repository, Gel displays
a directory browser modal. The modal lets you navigate the filesystem and select
a Git repository to open.

The browser shows directories in a tree view. Repositories are highlighted
with an orange diamond icon to make them easy to identify. Features include:

- **Path input bar** -- Navigate by typing a path and pressing Enter.
- **Sort options** -- Toggle between git-first (repos listed first) or
  alphabetical order.
- **Create folder** -- Create a new directory with inline rename.
- **Delete folder** -- Delete an empty directory.
- **Create repository** -- For a selected directory that is not itself a git
  repository, initialize a new git repository (`git init`) in it.
- **Keyboard navigation** -- Arrow keys to move, Enter to enter/exit folders,
  Tab to cycle through items.

Select a repository and press Enter, or double-click it to open it in Gel.

## Settings

Open the settings modal via the gear button in the toolbar.

- **Style** -- Color theme (see list below)
- **Layout** -- UI layout preset: Default, Compact, Comfortable, Rounded, or
  Flat. Layout is independent of color style
- **Font** -- Choose from the system's fonts or use the bundled DejaVu Sans
  Mono (default). Type in the search box to narrow the list; monospace fonts
  are grouped first, since diffs read better in a fixed pitch
- **Font scale** -- Slider from 0.5 to 2.0
- **Display** (Linux) -- Display backend: Auto, Wayland, or X11 (requires restart)
- **Renderer** -- Vulkan or OpenGL (not available on macOS)
- **GPU** -- Select which GPU to use (Vulkan only, shown when multiple GPUs are available)
- **Client-side decorations** (Linux) -- Use custom title bar drawn in the toolbar (requires restart)
- **Commit list columns** -- Expandable section to customize the commit history
  table columns: add a column (maximum 6), change a column's data type, or
  remove it. Available types: Author, Author Date, Commit, Committer,
  Committer Date
- **Diff tool** -- Text field for the diff tool name (passed as --tool= to
  `git difftool`). Leave empty to use git's configured default
- **File list** -- Position the file list on the Left or Right side
- **Diff colors** -- Controls how diff content is colored:
  - **Syntax highlighting** (default) -- Per-language syntax colors with tinted
    backgrounds. Supports C-like, Python, Shell, Ruby, HTML/XML, JSON, GN, and
    Shader languages
  - **ANSI colors** -- Display raw ANSI color codes from git
  - **ANSI colors with background** -- ANSI colors with background tinting
- **File extensions** -- Expandable section to add custom file extension to
  language mappings, overriding built-in associations. Map an extension to
  "None" to disable syntax highlighting for that file type
- **Show line numbers** -- Display line numbers in the diff viewer (enabled
  by default)

Style, layout, and font changes apply immediately as a live preview. The
settings modal supports keyboard navigation (Tab/Shift+Tab to move between
controls, arrow keys for dropdowns and sliders).

Characters missing from the selected font -- CJK, symbols, color emoji, and
other scripts -- are filled in automatically from installed system fonts.

### Available Themes

System (follows OS dark/light), Dark, Light, Catppuccin Mocha, Catppuccin
Latte, Catppuccin Frappé, Catppuccin Macchiato, Red Light District.

### Linux-Specific Options

- **Reinstall / Uninstall desktop icon** -- Installs or removes a .desktop
  file at ~/.local/share/applications/gel.desktop and an icon at
  ~/.local/share/icons/hicolor/256x256/apps/gel.png.
- **Reinstall / Uninstall bash completion** -- Installs or removes a
  completion script at ~/.local/share/bash-completion/completions/gel.
  If the bash-completion package is installed, it will be loaded
  automatically. Otherwise, add
  `source ~/.local/share/bash-completion/completions/gel` to your ~/.bashrc
  to enable it manually.

### Version Info

The settings dialog shows the Gel version and the installed Git version.

## Persistent Settings

Window geometry (position, size, maximized state), panel sizes, column
configuration, and all settings are saved automatically and restored on next
launch.

- **Linux** -- $XDG_CONFIG_HOME/gel/settings.json or ~/.config/gel/settings.json
- **Windows** -- %APPDATA%\gel\settings.json
- **macOS** -- ~/Library/Application Support/gel/settings.json

## Platform Support

- **Linux** (X11 and native Wayland)
- **Windows**
- **macOS**

The UI scales according to the display's DPI. Rendering uses Vulkan by default
with automatic fallback to OpenGL. On macOS only Vulkan is supported.
