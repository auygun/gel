// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_COMMIT_TREE_H
#define GEL_UI_LOWER_PANEL_COMMIT_TREE_H

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "base/file_reader.h"
#include "gel/commands/git_ls_tree.h"
#include "gel/commands/git_show_file.h"
#include "gel/ui/lower_panel/lower_panel.h"
#include "gel/ui/modules/image_view.h"
#include "gel/ui/modules/path_tree.h"
#include "gel/ui/modules/text_viewer.h"
#include "gel/ui/overlay_toolbar.h"
#include "gel/ui/syntax_highlight.h"

class GitRepo;
class GraphicsDelegate;
class PersistentSettings;

// Lower panel: every file of the selected commit's tree in a directory tree,
// and the content of the file selected in it, as gitk's Tree mode, or the
// image itself for an image file. For the unstaged and staged rows, the files
// of the working tree and of the index.
class CommitTree : public LowerPanel::View, public TextViewer::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void SetPrimarySelection(const std::string& text) = 0;
    // Finds the commit that last changed |line_number| of |file| as the tree
    // shows it: in the selected commit, in the working tree for the unstaged
    // row, or the staged content, |blob|, for the staged row.
    virtual void OnShowLineOrigin(const std::string& file,
                                  const std::string& blob,
                                  int line_number) = 0;
    // Runs git gui blame on |line_number| of |file|, in the selected commit
    // or in the working tree for the unstaged row. Not offered for the staged
    // row: git gui blame can't show the index.
    virtual void OnRunGitGuiBlame(const std::string& file, int line_number) = 0;
  };

  // What the tree shows: a commit, or one of the local rows.
  struct Source {
    std::string commit;
    bool unstaged = false;
    bool staged = false;

    bool empty() const { return commit.empty() && !unstaged && !staged; }
    bool operator==(const Source&) const = default;
  };

  // Images are drawn through |graphics_delegate|.
  CommitTree(Delegate& delegate,
             GraphicsDelegate& graphics_delegate,
             GitRepo& git_repo,
             PersistentSettings& settings,
             std::function<void(bool)> busy_callback,
             std::function<void()> main_thread_busy);
  ~CommitTree() override;

  // Shows the files of |source|. Reloads them if |reload|, e.g. after the
  // working tree or the index changed, keeping the selected file.
  void SetSource(const Source& source, bool reload = false);

  // Feeds the git results in. Call once per frame, before the panel renders
  // the view.
  void Update();

  // LowerPanel::View implementation: the file tree and the file's content.
  void RenderSidePane(float width) final;
  void RenderMainPane(float width) final;
  void FinishFrame() final;

  // Selects |file| once the tree lists it, and scrolls to and highlights
  // |line| of it, as the line found by a blame lookup.
  void ShowFileLine(const std::string& file, int line);

  // The content's search bar: whether it is open, whether its input has
  // focus, and focusing it on the next Update(). Hidden while an image is
  // shown.
  bool search_active() const {
    return !showing_image_ && text_viewer_.search_active();
  }
  bool search_input_active() const {
    return !showing_image_ && text_viewer_.search_input_active();
  }
  void FocusSearch() { text_viewer_.FocusSearch(); }

 private:
  // TextViewer::Delegate implementation.
  void OnTextSelected(const std::string& text) override;
  void RenderLine(int line_index, const std::string& line) override;
  float GetTextOffset(int line_index) const override;
  void OnContextMenu(int line_index) override;
  void OnSearchInput(char* buf, size_t buf_size) override;

  // Starts listing the files of |source_|.
  void StartListing();
  // Shows the content of entry |index|, or nothing if -1.
  void SelectEntry(int index);
  // Starts loading the content of the selected entry, |entry|, as text.
  void LoadText(const TreeEntry& entry);
  // Shows the text in place of an image that didn't load, or a note if it was
  // too large to show.
  void UpdateImage();
  // Selects the entry at |path| if listed. Returns false if it isn't (yet).
  bool SelectPath(const std::string& path);
  // Stops loading the selected file's content and clears it.
  void ClearContent();
  // Shows |note| in place of the selected file's content.
  void ShowNote(std::string note);
  // The selected file's lines loaded so far, from git or the working tree.
  std::span<const std::string> LoadedLines() const;
  // Whether all of the selected file's content is loaded, or failed to load.
  bool ContentLoaded() const;
  bool ContentFailed() const;
  // Checks the newly loaded lines for a binary file, and scans them for
  // syntax states.
  void ScanContent();
  // The lines the viewer shows: the content, or a note in place of it, or
  // nothing while it can't be told yet whether the file is binary.
  std::span<const std::string> ShownLines() const;
  void RenderTree();
  // Renders the selected file's path above its content, |width| wide.
  void RenderHeader(float width);
  void RenderOverlay();
  // Width of the line number column, for the selected file's line count.
  float LineNumberWidth() const;

  Delegate& delegate_;
  GitRepo& git_repo_;
  PersistentSettings& settings_;

  GitLsTree git_ls_tree_;
  // The selected file's content: a blob from git, or a working tree file.
  GitShowFile git_show_file_;
  FileReader file_reader_;
  // The selected file's image, in place of its content while |showing_image_|.
  ImageView image_view_;
  // Whether the selected file is shown as an image: one with an image's file
  // extension, until it fails to decode, e.g. a Git LFS pointer, and its
  // content is shown as text after all.
  bool showing_image_ = false;

  Source source_;

  PathTree tree_{false};
  // The listing |tree_| was built from.
  int tree_generation_ = 0;
  int selected_entry_ = -1;
  // Path of the selected file, to select again once a reload lists it.
  std::string selected_path_;
  // Whether |selected_path_| is still to be looked for in the listing.
  bool find_selected_ = false;
  bool scroll_tree_to_selected_ = false;

  // A line to show once its file is listed and loaded, from a blame lookup.
  std::string pending_file_;
  int pending_line_ = 0;

  TextViewer text_viewer_;
  // The selected file's language.
  Language lang_ = Language::kNone;
  // Whether the content is read from the working tree rather than git.
  bool from_work_tree_ = false;
  // A note shown in place of the content (binary file, submodule, error), as
  // the viewer's one line. Empty while the content is shown.
  std::vector<std::string> note_;
  // Bytes of content checked for a NUL byte so far, and whether the check is
  // done: the content is only shown once it is.
  size_t binary_checked_bytes_ = 0;
  int binary_checked_lines_ = 0;
  bool text_confirmed_ = false;
  // The syntax state each line starts in, recorded where it changes, and the
  // number of lines scanned so far.
  struct SyntaxMark {
    int line = 0;
    SyntaxState state;
  };
  std::vector<SyntaxMark> syntax_marks_;
  int syntax_scanned_lines_ = 0;
  SyntaxState syntax_state_;

  OverlayToolbar overlay_;
  ImVec2 overlay_anchor_{0, 0};
};

#endif  // GEL_UI_LOWER_PANEL_COMMIT_TREE_H
