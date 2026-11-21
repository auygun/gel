// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/image_diff_viewer.h"

#include <cstdio>

#include "gel/git_repo.h"
#include "third_party/imgui/imgui/imgui.h"

using namespace eng;

namespace {

// The object name "git cat-file" takes for |path| in |ref|. A ref ending in
// ':' names the index ("" + ":" + path).
std::string ObjectName(const std::string& ref, const std::string& path) {
  return !ref.empty() && ref.back() == ':' ? ref + path : ref + ":" + path;
}

}  // namespace

ImageDiffViewer::ImageDiffViewer(int id,
                                 const std::filesystem::path& toplevel,
                                 const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree,
                                 std::function<void(bool)> busy_callback)
    : path_(path), old_view_(busy_callback), new_view_(busy_callback) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s###img_diff_%d", path_.c_str(), id);
  title_ = buf;

  if (!old_ref.empty())
    old_view_.LoadBlob(ObjectName(old_ref, path), toplevel);
  if (new_from_worktree)
    new_view_.LoadFile(WorkTreePath(toplevel, path));
  else if (!new_ref.empty())
    new_view_.LoadBlob(ObjectName(new_ref, path), toplevel);
}

ImageDiffViewer::~ImageDiffViewer() = default;

void ImageDiffViewer::OnRendererChanged() {
  old_view_.OnRendererChanged();
  new_view_.OnRendererChanged();
}

void ImageDiffViewer::DestroyTextures(Renderer& renderer) {
  old_view_.DestroyTextures(renderer);
  new_view_.DestroyTextures(renderer);
}

void ImageDiffViewer::Update(Renderer& renderer, bool window_focused) {
  old_view_.Update(renderer);
  new_view_.Update(renderer);

  if (!open_)
    return;

  if (first_frame_) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(display.x * 0.7f, display.y * 0.8f),
                             ImGuiCond_FirstUseEver);
    first_frame_ = false;
  }

  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
  if (ImGui::Begin(title_.c_str(), &open_)) {
    ImGui::TextUnformatted(path_.c_str());
    float half_w =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;
    RenderSide("Old", old_view_, half_w);
    ImGui::SameLine();
    RenderSide("New", new_view_, half_w);
  }
  ImGui::End();
  if (!window_focused)
    ImGui::PopStyleColor();
}

// static
void ImageDiffViewer::RenderSide(const char* label,
                                 ImageView& view,
                                 float width) {
  ImGui::PushID(label);
  ImGui::BeginGroup();
  switch (view.state()) {
    case ImageView::State::kLoading:
      ImGui::Text("%s", label);
      ImGui::SameLine();
      ImGui::TextDisabled("(loading...)");
      break;
    case ImageView::State::kLoaded:
      ImGui::Text("%s  %dx%d", label, view.width(), view.height());
      break;
    case ImageView::State::kTooLarge:
      ImGui::Text("%s", label);
      ImGui::SameLine();
      if (view.width() > 0)
        ImGui::TextDisabled("(too large to show: %dx%d)", view.width(),
                            view.height());
      else
        ImGui::TextDisabled("(too large to show)");
      break;
    case ImageView::State::kEmpty:
    case ImageView::State::kFailed:
      ImGui::Text("%s", label);
      ImGui::SameLine();
      ImGui::TextDisabled("(no image)");
      break;
  }
  view.Render(width);
  ImGui::EndGroup();
  ImGui::PopID();
}
