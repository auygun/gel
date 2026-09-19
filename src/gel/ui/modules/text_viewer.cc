// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/text_viewer.h"
#include "gel/ui/icons.h"
#include "gel/ui/style.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

void TextViewer::Delegate::RenderLine(int /*line_index*/,
                                      const std::string& line) {
  ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
}

float TextViewer::Delegate::GetTextOffset(int /*line_index*/) const {
  return 0.0f;
}

TextViewer::TextViewer(Delegate& delegate,
                       std::function<void()> search_in_progress_callback)
    : delegate_(delegate),
      search_in_progress_callback_(std::move(search_in_progress_callback)) {}

TextViewer::~TextViewer() = default;

void TextViewer::Reset() {
  scroll_to_line_ = 0;
  scroll_exact_target_ = -1;
  prev_scroll_y_ = 0;
  track_top_line_ = true;
  top_visible_line_ = -1;
  sel_start_line_ = -1;
  sel_dragging_ = false;
  sel_word_mode_ = false;
  highlight_line_ = -1;
  scroll_to_highlight_ = false;
  content_width_ = 0;
  search_.Reset();
}

void TextViewer::CancelSearch() {
  search_.Cancel();
}

void TextViewer::ScrollToLine(int line) {
  scroll_to_line_ = line;
  track_top_line_ = false;
}

void TextViewer::ScrollToTop() {
  scroll_to_line_ = 0;
  scroll_exact_target_ = -1;
  track_top_line_ = true;
  top_visible_line_ = -1;
}

int TextViewer::GetTopVisibleLine() const {
  if (suppress_scroll_report_ > 0 || !track_top_line_)
    return -1;
  return top_visible_line_;
}

void TextViewer::UpdateSearchBar(std::span<const std::string> content,
                                 base::MatchOptions* match_options,
                                 bool has_focus) {
  // Process a chunk of search work before drawing the bar, so the match
  // counter and the navigation buttons see the current term's results.
  search_.Update(search_bar_.term(), *match_options, content);
  if (!search_.search_complete() && search_in_progress_callback_)
    search_in_progress_callback_();

  SearchBar::Options opts;
  opts.id = "tv_search";
  opts.match_counter = true;
  opts.close_button = true;
  opts.input_tooltip = "Search in content";
  opts.matches = {search_.current(), static_cast<int>(search_.matches().size()),
                  search_.search_complete()};
  opts.on_input = [this](char* buf, size_t buf_size) {
    delegate_.OnSearchInput(buf, buf_size);
  };
  SearchBar::Result search = search_bar_.Update(opts, match_options);

  if (!search_.matches().empty() && (search.next || search.prev)) {
    if (search_.current() < 0) {
      // No current match yet. Jump to the first visible match.
      float lh = 2.0f * std::ceil(ImGui::GetTextLineHeight() * 0.5f);
      int top_line = lh > 0 ? static_cast<int>(prev_scroll_y_ / lh) : 0;
      search_.NavigateToLine(top_line);
    } else {
      search_.Navigate(search.next ? 1 : -1);
    }
  }

  // Escape or close button closes the search bar.
  // Block if a non-modal popup (context menu) is open, or if a modal
  // popup is open above us (i.e. we are not inside it). When the
  // TextViewer is hosted inside a modal, that modal is not blocking.
  bool popup_open = false;
  {
    ImGuiContext& g = *GImGui;
    for (int i = g.OpenPopupStack.Size - 1; i >= 0; i--) {
      ImGuiWindow* w = g.OpenPopupStack[i].Window;
      if (w && !(w->Flags & ImGuiWindowFlags_Modal)) {
        popup_open = true;
        break;
      }
    }
    if (!popup_open) {
      ImGuiWindow* modal = ImGui::GetTopMostPopupModal();
      if (modal &&
          !ImGui::IsWindowWithinBeginStackOf(ImGui::GetCurrentWindow(), modal))
        popup_open = true;
    }
  }
  if (search.close_requested ||
      (!popup_open && search.input_active &&
       ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ||
      (!popup_open && search.input_deactivated &&
       ImGui::IsKeyDown(ImGuiKey_Escape)) ||
      (!popup_open && has_focus &&
       ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
       !ImGui::GetIO().WantTextInput)) {
    search_active_ = false;
    search_.Cancel();
  }
}

void TextViewer::Update(float width,
                        std::span<const std::string> content,
                        base::MatchOptions* match_options) {
  user_moved_view_ = false;

  // Ctrl+F opens/focuses the search bar.
  ImGuiWindow* modal = ImGui::GetTopMostPopupModal();
  bool blocked_by_modal = modal && !ImGui::IsWindowWithinBeginStackOf(
                                       ImGui::GetCurrentWindow(), modal);
  bool has_focus =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (!blocked_by_modal && has_focus && ImGui::GetIO().KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    search_active_ = true;
    search_bar_.Focus();
  }

  ImGui::BeginGroup();

  // Search bar.
  if (search_active_)
    UpdateSearchBar(content, match_options, has_focus);

  float line_height = 2.0f * std::ceil(ImGui::GetTextLineHeight() * 0.5f);

  // Pin the horizontal content size so it stays stable while scrolling.
  float char_width =
      ImGui::GetFont()
          ->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ")
          .x;

  // Compute the content height in double to avoid float precision loss.
  double precise_height =
      static_cast<double>(line_count_) * static_cast<double>(line_height);
  float content_height = static_cast<float>(precise_height);
  if (static_cast<double>(content_height) < precise_height)
    content_height = std::nextafter(content_height, FLT_MAX);
  ImGui::SetNextWindowContentSize(ImVec2(content_width_, content_height));

  // Use SetNextWindowScroll (before BeginChild) so the scroll takes effect on
  // the same frame, avoiding a 1-frame delay.
  if (scroll_to_line_ >= 0) {
    scroll_exact_target_ =
        static_cast<double>(scroll_to_line_) * static_cast<double>(line_height);
    float outer_height = ImGui::GetContentRegionAvail().y;
    float inner_height = outer_height -
                         ImGui::GetStyle().ChildBorderSize * 2.0f -
                         ImGui::GetStyle().WindowPadding.y * 2.0f -
                         ImGui::GetStyle().ScrollbarSize;
    double max_scroll = precise_height - static_cast<double>(inner_height);
    if (max_scroll <= 0) {
      scroll_exact_target_ = -1;
    } else {
      if (scroll_exact_target_ > max_scroll)
        scroll_exact_target_ = max_scroll;
      ImGui::SetNextWindowScroll(
          ImVec2(-1.0f, static_cast<float>(scroll_exact_target_)));
      suppress_scroll_report_ = 2;
    }
    scroll_to_line_ = -1;
  }
  if (ImGui::BeginChild("text_viewer_content", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();
    float sb = ImGui::GetStyle().ScrollbarSize;
    content_top_right_ = ImVec2(wpos.x + wsz.x - sb, wpos.y);

    // Scroll to the current search match, centering only if not visible.
    if (search_.ConsumeScrollRequest() && search_.current() >= 0 &&
        search_.current() < static_cast<int>(search_.matches().size())) {
      auto& current_match = search_.matches()[search_.current()];
      double target_y = static_cast<double>(current_match.line) *
                        static_cast<double>(line_height);
      float cur_scroll = ImGui::GetScrollY();
      float window_h = ImGui::GetWindowHeight();
      float target_yf = static_cast<float>(target_y);
      bool need_scroll = target_yf < cur_scroll ||
                         target_yf + line_height > cur_scroll + window_h;
      if (need_scroll) {
        user_moved_view_ = true;
        ImGui::SetScrollY(
            static_cast<float>(target_y - window_h / 2 + line_height / 2));
        scroll_exact_target_ = -1;
        suppress_scroll_report_ = 2;
      }

      // Scroll horizontally to center the match if not visible.
      ImFont* font_for_scroll = ImGui::GetFont();
      float font_size_for_scroll = ImGui::GetFontSize();
      const std::string& match_line_text = content[current_match.line];
      float match_x =
          font_for_scroll
              ->CalcTextSizeA(font_size_for_scroll, FLT_MAX, -1.0f,
                              match_line_text.c_str(),
                              match_line_text.c_str() + current_match.offset)
              .x;
      float match_end_x =
          font_for_scroll
              ->CalcTextSizeA(font_size_for_scroll, FLT_MAX, -1.0f,
                              match_line_text.c_str(),
                              match_line_text.c_str() + current_match.offset +
                                  static_cast<int>(search_.term().size()))
              .x;
      float cur_scroll_x = ImGui::GetScrollX();
      float window_w = ImGui::GetWindowWidth();
      float scrollbar_size = ImGui::GetStyle().ScrollbarSize;
      ImGuiWindow* win = ImGui::GetCurrentWindowRead();
      if (win->ScrollbarY)
        window_w -= scrollbar_size;
      bool need_scroll_x =
          match_x < cur_scroll_x || match_end_x > cur_scroll_x + window_w;
      if (need_scroll_x) {
        float match_center = (match_x + match_end_x) / 2;
        ImGui::SetScrollX(match_center - window_w / 2);
      }
    }

    // Handle highlight line: scroll to center.
    if (scroll_to_highlight_ && highlight_line_ >= 0 &&
        highlight_line_ < line_count_) {
      double target_y = static_cast<double>(highlight_line_) *
                        static_cast<double>(line_height);
      float window_h = ImGui::GetWindowHeight();
      ImGui::SetScrollY(
          static_cast<float>(target_y - window_h / 2 + line_height / 2));
      scroll_exact_target_ = -1;
      suppress_scroll_report_ = 2;

      scroll_to_highlight_ = false;
    }

    // Re-enable top line tracking when the user scrolls manually.
    float scroll_y = ImGui::GetScrollY();
    if (suppress_scroll_report_ > 0) {
      suppress_scroll_report_--;
    } else if (scroll_y != prev_scroll_y_) {
      if (!track_top_line_)
        track_top_line_ = true;
      if (wheel_scroll_frames_ == 0)
        scroll_exact_target_ = -1;
    }
    if (wheel_scroll_frames_ > 0)
      wheel_scroll_frames_--;
    prev_scroll_y_ = scroll_y;

    // ImGui derives ScrollMaxY from the float content size, so for very large
    // content (millions of lines) it is off by up to one float ULP of the
    // content height (tens of pixels). Recompute the limit in double and use
    // it everywhere instead.
    double max_scroll_exact = 0;
    {
      ImGuiWindow* win = ImGui::GetCurrentWindowRead();
      max_scroll_exact =
          std::max(0.0, precise_height +
                            2.0 * static_cast<double>(win->WindowPadding.y) -
                            static_cast<double>(win->InnerRect.GetHeight()));
    }

    // Pin the content to the exact bottom offset once ImGui has run out of
    // scroll, otherwise the last line lands short of or past the bottom edge.
    // A target the user already moved up is left alone: it accumulates wheel
    // steps smaller than the float scroll resolution, which resetting it every
    // frame would discard, making the wheel unable to scroll back up.
    // Skip while a programmatic scroll is in flight (search match jump,
    // highlight): SetScrollY only sets ScrollTarget, applied on the next
    // Begin, so Scroll.y still holds the previous position this frame. Pinning
    // here would re-arm scroll_exact_target_ at the stale bottom offset, and
    // the compensation below would then shift the rendered lines a full page
    // down (scrollbar moves, content does not).
    if (suppress_scroll_report_ == 0 && scroll_y >= ImGui::GetScrollMaxY() &&
        (scroll_exact_target_ < 0 || scroll_exact_target_ > max_scroll_exact)) {
      scroll_exact_target_ = max_scroll_exact;
    }

    // Compensate for float precision loss in Scroll.y.
    float scroll_correction = 0;
    if (scroll_exact_target_ >= 0) {
      double error = static_cast<double>(scroll_y) - scroll_exact_target_;
      scroll_correction = static_cast<float>(error);
      ImGui::GetCurrentWindow()->DC.CursorStartPosLossyness.y +=
          scroll_correction;
    }

    // Handle mouse wheel with double precision for large content.
    if (ImGui::IsWindowHovered()) {
      ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,
                         ImGui::GetID("##tv_content_wheel"));
      float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0.0f) {
        user_moved_view_ = true;
        if (wheel_intercepted_) {
          ImGuiWindow* win = ImGui::GetCurrentWindowRead();
          float max_step = win->InnerRect.GetHeight() * 0.67f;
          // ImGui steps 5 * font size here, but line_height rounds the font
          // size up to an even number, so an odd font size would leave the
          // view a fraction of a line out of alignment on every notch. Step in
          // whole lines instead.
          float scroll_step =
              std::floor(std::min(5.0f * line_height, max_step));
          double current = scroll_exact_target_ >= 0
                               ? scroll_exact_target_
                               : static_cast<double>(scroll_y);
          scroll_exact_target_ = current - static_cast<double>(wheel) *
                                               static_cast<double>(scroll_step);
          scroll_exact_target_ = std::max(0.0, scroll_exact_target_);
          if (scroll_exact_target_ > max_scroll_exact)
            scroll_exact_target_ = max_scroll_exact;
          ImGui::SetScrollY(static_cast<float>(scroll_exact_target_));
          wheel_scroll_frames_ = 2;
        } else {
          scroll_exact_target_ = static_cast<double>(scroll_y);
          wheel_scroll_frames_ = 2;
        }
      }
      wheel_intercepted_ = true;
    } else {
      wheel_intercepted_ = false;
    }

    // Keyboard scrolling (arrow keys, page up/down, home/end).
    if (keyboard_scroll_ && !ImGui::GetIO().WantTextInput) {
      double current = scroll_exact_target_ >= 0
                           ? scroll_exact_target_
                           : static_cast<double>(scroll_y);
      double target = -1;
      float page =
          std::max(line_height, ImGui::GetWindowHeight() - line_height);
      if (ImGui::IsKeyPressed(ImGuiKey_Home))
        target = 0;
      else if (ImGui::IsKeyPressed(ImGuiKey_End))
        target = max_scroll_exact;
      else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        target = std::max(0.0, current - static_cast<double>(page));
      else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        target =
            std::min(max_scroll_exact, current + static_cast<double>(page));
      else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        target = std::max(0.0, current - static_cast<double>(line_height));
      else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        target = std::min(max_scroll_exact,
                          current + static_cast<double>(line_height));
      if (target >= 0) {
        user_moved_view_ = true;
        scroll_exact_target_ = target;
        ImGui::SetScrollY(static_cast<float>(target));
        suppress_scroll_report_ = 2;
      }
    }

    // Track the top visible line for the caller. Use the exact scroll offset
    // when there is one, so the reported line matches what is drawn.
    if (track_top_line_) {
      double offset = scroll_exact_target_ >= 0 ? scroll_exact_target_
                                                : static_cast<double>(scroll_y);
      top_visible_line_ =
          static_cast<int>(offset / static_cast<double>(line_height));
    }

    ImGuiID active_id = ImGui::GetActiveID();
    ImGuiID scrollbar_id =
        ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindowRead(), ImGuiAxis_Y);
    bool scrollbar_active = active_id && active_id == scrollbar_id;
    if (scrollbar_active)
      user_moved_view_ = true;

    // Freeze the item count while the scrollbar is held to prevent jumps.
    // Always clamp to the actual size to avoid out-of-bounds access when the
    // content is cleared (e.g. F5 refresh) while the scrollbar is held.
    if (!scrollbar_active)
      line_count_ = static_cast<int>(content.size());
    else
      line_count_ = std::min(line_count_, static_cast<int>(content.size()));

    // Character-level text selection via mouse.
    ImFont* font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize();
    ImVec2 content_origin = ImGui::GetCursorScreenPos();
    double content_origin_y_precise =
        static_cast<double>(content_origin.y) +
        static_cast<double>(
            ImGui::GetCurrentWindow()->DC.CursorStartPosLossyness.y);

    auto mouse_to_pos = [&](ImVec2 mouse, int& line, int& ch) -> bool {
      if (line_count_ <= 0) {
        line = -1;
        ch = 0;
        return false;
      }
      line = static_cast<int>(
          (static_cast<double>(mouse.y) - content_origin_y_precise) /
          static_cast<double>(line_height));
      line = std::max(0, std::min(line, line_count_ - 1));
      float rel_x = mouse.x - content_origin.x;
      if (rel_x <= 0.0f) {
        ch = 0;
        return true;
      }
      // Subtract text offset (e.g. badge rendered before text) so we
      // measure from the actual text start position.
      float text_offset = delegate_.GetTextOffset(line);
      if (rel_x < text_offset) {
        ch = 0;
        return true;
      }
      rel_x -= text_offset;

      const std::string& lt = content[line];
      const char* text_ptr = lt.c_str();
      int text_len = static_cast<int>(lt.size());
      if (!lt.empty() && lt[0] == '\x01') {
        text_ptr = lt.c_str() + 1;
        --text_len;
      }
      if (text_len <= 0) {
        ch = 0;
        return true;
      }
      const char* remaining = nullptr;
      font->CalcTextSizeA(font_size, rel_x, -1.0f, text_ptr,
                          text_ptr + text_len, &remaining);
      ch = static_cast<int>(remaining - text_ptr);
      return true;
    };

    auto word_bounds = [](const std::string& text,
                          int ch) -> std::pair<int, int> {
      int len = static_cast<int>(text.size());
      ch = std::min(ch, len);
      if (ch >= len)
        return {len, len};
      auto classify = [](char c) -> int {
        // Same notion of a word as whole-word search, so double-clicking a
        // word and searching it with that toggle on agree.
        if (base::IsWordByte(c))
          return 0;
        if (std::isspace(static_cast<unsigned char>(c)))
          return 1;
        return 2;
      };
      int cat = classify(text[ch]);
      int start = ch, end = ch;
      while (start > 0 && classify(text[start - 1]) == cat)
        --start;
      while (end < len && classify(text[end]) == cat)
        ++end;
      return {start, end};
    };

    // Capture line index on right-click for the context menu.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      int line, ch;
      if (mouse_to_pos(ImGui::GetMousePos(), line, ch)) {
        ctx_menu_line_ = line;
        highlight_line_ = -1;
        delegate_.OnRightClick(line, ch);
      }
    }

    if (ImGui::IsWindowHovered()) {
      // Show text cursor over the content area but not over the scrollbars.
      {
        ImVec2 mouse = ImGui::GetMousePos();
        float scrollbar_size = ImGui::GetStyle().ScrollbarSize;
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();
        ImGuiWindow* win = ImGui::GetCurrentWindowRead();
        bool over_scrollbar =
            (win->ScrollbarY &&
             mouse.x > win_pos.x + win_size.x - scrollbar_size) ||
            (win->ScrollbarX &&
             mouse.y > win_pos.y + win_size.y - scrollbar_size);
        if (!over_scrollbar)
          ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
      }

      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        int line, ch;
        if (mouse_to_pos(ImGui::GetMousePos(), line, ch)) {
          const std::string& lt = content[line];
          int prefix = lt.empty() || lt[0] != '\x01' ? 0 : 1;
          auto [ws, we] = word_bounds(lt, ch + prefix);
          sel_start_line_ = sel_end_line_ = line;
          sel_start_char_ = std::max(0, ws - prefix);
          sel_end_char_ = we - prefix;
          sel_dragging_ = true;
          sel_word_mode_ = true;
          sel_anchor_start_ = std::max(0, ws - prefix);
          sel_anchor_end_ = we - prefix;
          sel_anchor_line_ = line;
        }
      } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int line, ch;
        if (mouse_to_pos(ImGui::GetMousePos(), line, ch)) {
          if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
              delegate_.OnCtrlClick(line, ch)) {
            // Consumed by delegate (e.g. link click).
          } else {
            sel_word_mode_ = false;
            highlight_line_ = -1;
            if (ImGui::GetIO().KeyShift && sel_start_line_ >= 0) {
              sel_end_line_ = line;
              sel_end_char_ = ch;
            } else {
              sel_start_line_ = sel_end_line_ = line;
              sel_start_char_ = sel_end_char_ = ch;
              sel_dragging_ = true;
            }
          }
        }
      }
    }

    if (sel_dragging_) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        int line, ch;
        if (mouse_to_pos(mouse, line, ch)) {
          if (sel_word_mode_) {
            const std::string& lt = content[line];
            int prefix = lt.empty() || lt[0] != '\x01' ? 0 : 1;
            auto [ws, we] = word_bounds(lt, ch + prefix);
            if (line < sel_anchor_line_ ||
                (line == sel_anchor_line_ && ch < sel_anchor_start_)) {
              sel_start_line_ = line;
              sel_start_char_ = std::max(0, ws - prefix);
              sel_end_line_ = sel_anchor_line_;
              sel_end_char_ = sel_anchor_end_;
            } else {
              sel_start_line_ = sel_anchor_line_;
              sel_start_char_ = sel_anchor_start_;
              sel_end_line_ = line;
              sel_end_char_ = we - prefix;
            }
          } else {
            sel_end_line_ = line;
            sel_end_char_ = ch;
          }
        }

        // Auto-scroll when drag-selecting near the edges.
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          float win_top = ImGui::GetWindowPos().y;
          float win_bot = win_top + ImGui::GetWindowHeight();
          float win_left = ImGui::GetWindowPos().x;
          float win_right = win_left + ImGui::GetWindowWidth();
          float dt = ImGui::GetIO().DeltaTime;
          float margin_y = line_height * 3.0f;
          float margin_x = char_width * 6.0f;
          if (mouse.y < win_top + margin_y) {
            float dist = (win_top + margin_y) - mouse.y;
            float speed = (1.0f + dist / line_height) * 30.0f * line_height;
            double current = scroll_exact_target_ >= 0
                                 ? scroll_exact_target_
                                 : static_cast<double>(ImGui::GetScrollY());
            scroll_exact_target_ =
                std::max(0.0, current - static_cast<double>(speed) *
                                            static_cast<double>(dt));
            ImGui::SetScrollY(static_cast<float>(scroll_exact_target_));
            suppress_scroll_report_ = 2;
          } else if (mouse.y > win_bot - margin_y) {
            float dist = mouse.y - (win_bot - margin_y);
            float speed = (1.0f + dist / line_height) * 30.0f * line_height;
            double current = scroll_exact_target_ >= 0
                                 ? scroll_exact_target_
                                 : static_cast<double>(ImGui::GetScrollY());
            scroll_exact_target_ =
                current + static_cast<double>(speed) * static_cast<double>(dt);
            if (scroll_exact_target_ > max_scroll_exact)
              scroll_exact_target_ = max_scroll_exact;
            ImGui::SetScrollY(static_cast<float>(scroll_exact_target_));
            suppress_scroll_report_ = 2;
          }
          if (mouse.x < win_left + margin_x) {
            float dist = (win_left + margin_x) - mouse.x;
            float speed = (1.0f + dist / char_width) * 30.0f * char_width;
            ImGui::SetScrollX(ImGui::GetScrollX() - speed * dt);
          } else if (mouse.x > win_right - margin_x) {
            float dist = mouse.x - (win_right - margin_x);
            float speed = (1.0f + dist / char_width) * 30.0f * char_width;
            ImGui::SetScrollX(ImGui::GetScrollX() + speed * dt);
          }
        }
      } else {
        sel_dragging_ = false;

        // Notify delegate with the selected text.
        int sl = sel_start_line_, sc = sel_start_char_;
        int el = sel_end_line_, ec = sel_end_char_;
        if (sl > el || (sl == el && sc > ec)) {
          std::swap(sl, el);
          std::swap(sc, ec);
        }
        if (sl >= 0) {
          std::string text = ExtractSelectedText(content, sl, sc, el, ec);
          if (!text.empty())
            delegate_.OnTextSelected(text);
        }
      }
    }

    // Normalize selection so start <= end.
    int s_line = sel_start_line_, s_char = sel_start_char_;
    int e_line = sel_end_line_, e_char = sel_end_char_;
    if (s_line > e_line || (s_line == e_line && s_char > e_char)) {
      std::swap(s_line, e_line);
      std::swap(s_char, e_char);
    }

    // Use a list clipper to only submit the visible lines.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(0, line_height - ImGui::GetTextLineHeight()));

    ImGuiListClipper clipper;
    clipper.Begin(line_count_, line_height);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 sel_color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    ImU32 search_hl_color = GetSyntaxPalette().search_match_bg;

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const std::string& line = content[i];
        const char* text_start = !line.empty() && line[0] == '\x01'
                                     ? line.c_str() + 1
                                     : line.c_str();
        float text_width =
            font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, text_start).x;
        content_width_ =
            std::max(content_width_, text_width + delegate_.GetTextOffset(i));

        // Let the delegate draw custom backgrounds.
        {
          ImVec2 lp = ImGui::GetCursorScreenPos();
          float win_x = ImGui::GetWindowPos().x;
          float win_w = ImGui::GetWindowWidth();
          delegate_.DrawLineBackground(i, line, draw_list, lp, line_height,
                                       win_x, win_w);
        }

        // Draw highlight line background.
        if (i == highlight_line_) {
          ImVec2 lp = ImGui::GetCursorScreenPos();
          float win_x = ImGui::GetWindowPos().x;
          float win_w = ImGui::GetWindowWidth();
          ImU32 col = ImGui::GetColorU32(ImGuiCol_Header);
          draw_list->AddRectFilled(ImVec2(win_x, lp.y),
                                   ImVec2(win_x + win_w, lp.y + line_height),
                                   col);
        }

        // Draw selection highlight behind the text.
        if (s_line >= 0 && i >= s_line && i <= e_line) {
          ImVec2 lp = ImGui::GetCursorScreenPos();
          int raw_len = static_cast<int>(line.size());

          int c1 = (i == s_line) ? s_char : 0;
          int c2 = (i == e_line) ? e_char : raw_len;
          c1 = std::min(c1, raw_len);
          c2 = std::min(c2, raw_len);

          if (i > s_line && i < e_line && c2 == 0)
            c2 = 1;

          if (c2 > c1) {
            // s_char/e_char are stripped-relative; skip \x01 prefix if
            // present so CalcTextSizeA measures rendered characters only.
            const char* text_ptr = line.c_str();
            int stripped_len = raw_len;
            if (!line.empty() && line[0] == '\x01') {
              text_ptr = line.c_str() + 1;
              --stripped_len;
            }
            int sc1 = std::min(c1, stripped_len);
            int sc2 = std::min(c2, stripped_len);
            float x1 = font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, text_ptr,
                                           text_ptr + sc1)
                           .x;
            float x2 =
                (sc2 <= stripped_len)
                    ? font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, text_ptr,
                                          text_ptr + sc2)
                          .x
                    : font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, text_ptr)
                              .x +
                          char_width;
            // Offset for badge rendered before the text.
            float text_offset = delegate_.GetTextOffset(i);
            x1 += text_offset;
            x2 += text_offset;
            draw_list->AddRectFilled(ImVec2(lp.x + x1, lp.y),
                                     ImVec2(lp.x + x2, lp.y + line_height),
                                     sel_color);
          }
        }

        // Let the delegate draw custom highlights (e.g. external search term).
        {
          ImVec2 lp = ImGui::GetCursorScreenPos();
          delegate_.DrawLineHighlights(i, line, draw_list, lp, line_height,
                                       font, font_size);
        }

        // Highlight search matches.
        if (search_active_ && !search_.term().empty()) {
          ImVec2 lp = ImGui::GetCursorScreenPos();
          size_t term_len = search_.term().size();
          float text_offset = delegate_.GetTextOffset(i);
          // When the line has a \x01 prefix (e.g. file header), the delegate's
          // RenderLine skips it, so we must also skip it when measuring text
          // positions for the highlight.
          const char* line_base = line.c_str();
          int prefix = line.empty() || line[0] != '\x01' ? 0 : 1;
          base::ForEachMatch(line, search_.term(), match_buf_, [&](size_t pos) {
            float x1 = font->CalcTextSizeA(font_size, FLT_MAX, -1.0f,
                                           line_base + prefix,
                                           line_base + prefix + pos - prefix)
                           .x;
            float x2 = font->CalcTextSizeA(
                               font_size, FLT_MAX, -1.0f, line_base + prefix,
                               line_base + prefix + pos + term_len - prefix)
                           .x;
            bool is_current = search_.current() >= 0 &&
                              search_.current() <
                                  static_cast<int>(search_.matches().size()) &&
                              search_.matches()[search_.current()].line == i &&
                              search_.matches()[search_.current()].offset ==
                                  static_cast<int>(pos);
            ImU32 hl_color = is_current ? GetSyntaxPalette().search_current_bg
                                        : search_hl_color;
            draw_list->AddRectFilled(
                ImVec2(lp.x + text_offset + x1, lp.y),
                ImVec2(lp.x + text_offset + x2, lp.y + line_height), hl_color);
          });
        }

        // Render the line text.
        delegate_.RenderLine(i, line);
      }
    }
    ImGui::PopStyleVar();

    HandleKeyboardShortcuts(content, s_line, s_char, e_line, e_char);
    UpdateContextMenu(content, s_line, s_char, e_line, e_char);
  }
  ImGui::EndChild();

  ImGui::EndGroup();
}

void TextViewer::HandleKeyboardShortcuts(std::span<const std::string> content,
                                         int s_line,
                                         int s_char,
                                         int e_line,
                                         int e_char) {
  ImGuiWindow* modal = ImGui::GetTopMostPopupModal();
  bool blocked_by_modal = modal && !ImGui::IsWindowWithinBeginStackOf(
                                       ImGui::GetCurrentWindow(), modal);

  // Ctrl+A selects all content.
  if (!blocked_by_modal && !ImGui::GetIO().WantTextInput &&
      ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
    SelectAll(content);
  }

  // Ctrl+C copies the selected text to the clipboard.
  if (!blocked_by_modal && s_line >= 0 && !ImGui::GetIO().WantTextInput &&
      ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
    ImGui::SetClipboardText(
        ExtractSelectedText(content, s_line, s_char, e_line, e_char).c_str());
  }
}

void TextViewer::UpdateContextMenu(std::span<const std::string> content,
                                   int s_line,
                                   int s_char,
                                   int e_line,
                                   int e_char) {
  bool has_selection = s_line >= 0 && (s_line != e_line || s_char != e_char);
  if (ImGui::BeginPopupContextWindow()) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##ctx_menu_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Select all")) {
      SelectAll(content);
    }
    if (ImGui::MenuItem("Copy", nullptr, false, has_selection)) {
      ImGui::SetClipboardText(
          ExtractSelectedText(content, s_line, s_char, e_line, e_char).c_str());
    }

    // Let the delegate add custom context menu items.
    delegate_.OnContextMenu(ctx_menu_line_);

    ImGui::EndPopup();
  }
}

void TextViewer::SelectAll(std::span<const std::string> content) {
  sel_start_line_ = 0;
  sel_start_char_ = 0;
  sel_end_line_ = static_cast<int>(content.size()) - 1;
  if (sel_end_line_ >= 0 && sel_end_line_ < static_cast<int>(content.size())) {
    const std::string& last = content[sel_end_line_];
    sel_end_char_ = last.empty() || last[0] != '\x01'
                        ? static_cast<int>(last.size())
                        : static_cast<int>(last.size()) - 1;
  } else {
    sel_end_char_ = 0;
  }
}

std::string TextViewer::ExtractSelectedText(
    std::span<const std::string> content,
    int s_line,
    int s_char,
    int e_line,
    int e_char) const {
  std::string text;
  for (int i = s_line; i <= e_line && i < static_cast<int>(content.size());
       i++) {
    const std::string& lt = content[i];
    int prefix = lt.empty() || lt[0] != '\x01' ? 0 : 1;
    int effective_len = static_cast<int>(lt.size()) - prefix;
    int c1 = (i == s_line) ? std::min(s_char, effective_len) + prefix : prefix;
    int c2 = (i == e_line) ? std::min(e_char, effective_len) + prefix
                           : static_cast<int>(lt.size());
    if (i > s_line)
      text += '\n';
    text += lt.substr(c1, c2 - c1);
  }
  return text;
}

void TextViewer::HighlightLine(int line) {
  highlight_line_ = line;
  scroll_to_highlight_ = true;
}
