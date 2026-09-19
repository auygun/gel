// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/markdown_renderer.h"
#include "gel/ui/style.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

namespace {

enum class SpanStyle { kNormal, kBold, kCode, kLink };

struct TextSpan {
  std::string_view text;
  SpanStyle style;
  std::string_view link_target;
};

struct HighlightRegion {
  int offset;
  int length;
  bool is_current;
};

// Parse inline markdown (**bold** and `code`) into styled spans.
std::vector<TextSpan> ParseInlineSpans(std::string_view text) {
  std::vector<TextSpan> spans;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '[') {
      size_t close_bracket = text.find(']', i + 1);
      if (close_bracket != std::string_view::npos &&
          close_bracket + 1 < text.size() && text[close_bracket + 1] == '(') {
        size_t close_paren = text.find(')', close_bracket + 2);
        if (close_paren != std::string_view::npos) {
          auto link_text = text.substr(i + 1, close_bracket - i - 1);
          auto link_target =
              text.substr(close_bracket + 2, close_paren - close_bracket - 2);
          spans.push_back({link_text, SpanStyle::kLink, link_target});
          i = close_paren + 1;
          continue;
        }
      }
    }
    if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*') {
      size_t close = text.find("**", i + 2);
      if (close != std::string_view::npos) {
        spans.push_back(
            {text.substr(i + 2, close - i - 2), SpanStyle::kBold, {}});
        i = close + 2;
        continue;
      }
    }
    if (text[i] == '`') {
      size_t close = text.find('`', i + 1);
      if (close != std::string_view::npos) {
        spans.push_back(
            {text.substr(i + 1, close - i - 1), SpanStyle::kCode, {}});
        i = close + 1;
        continue;
      }
    }
    // Accumulate normal text.
    size_t start = i;
    while (i < text.size() && text[i] != '*' && text[i] != '`' &&
           text[i] != '[')
      i++;
    // Handle lone * that isn't **
    if (i < text.size() && text[i] == '*' &&
        (i + 1 >= text.size() || text[i + 1] != '*'))
      i++;
    if (i > start)
      spans.push_back({text.substr(start, i - start), SpanStyle::kNormal, {}});
  }
  return spans;
}

// Build visible text from inline spans (markdown markers stripped).
std::string BuildSearchText(std::string_view text) {
  auto spans = ParseInlineSpans(text);
  std::string result;
  for (auto& span : spans)
    result.append(span.text);
  return result;
}

// Generate a URL-style anchor slug from heading text (GitHub-style).
std::string HeadingToSlug(std::string_view text) {
  auto spans = ParseInlineSpans(text);
  std::string slug;
  for (auto& span : spans) {
    for (char c : span.text) {
      if (c == ' ' || c == '\t')
        slug += '-';
      else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
               c == '_')
        slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return slug;
}

// Render text spans with word wrapping and optional search highlighting.
void RenderWrappedSpans(const std::vector<TextSpan>& spans,
                        const std::vector<HighlightRegion>& highlights,
                        int scroll_to_offset,
                        bool& scrolled,
                        std::vector<MarkdownRenderer::TextRect>* text_rects,
                        int block_index,
                        float font_scale,
                        int sel_c1,
                        int sel_c2,
                        std::string* clicked_link) {
  ImFont* font = ImGui::GetFont();
  float font_size = ImGui::GetFontSize();
  float region_x = ImGui::GetContentRegionAvail().x;
  float start_x = ImGui::GetCursorPosX();
  float x = 0;
  float space_w = font->CalcTextSizeA(font_size, FLT_MAX, 0, " ").x;
  bool first_on_line = true;
  int text_offset = 0;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // Check if any span has inline code to decide whether to split channels.
  bool has_code = false;
  for (auto& s : spans) {
    if (s.style == SpanStyle::kCode) {
      has_code = true;
      break;
    }
  }
  if (has_code) {
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);  // Text on foreground channel.
  }

  // For drawing a continuous background behind inline code spans.
  float code_bg_start_x = -1;
  float code_bg_end_x = 0;
  float code_bg_y = 0;
  float code_bg_line_h = 0;

  // Deferred inline code background rects (drawn behind text).
  struct CodeBgRect {
    ImVec2 min, max;
  };
  std::vector<CodeBgRect> code_bg_rects;

  auto flush_code_bg = [&](float end_x) {
    if (code_bg_start_x >= 0) {
      float pad = code_bg_line_h * 0.1f;
      code_bg_rects.push_back({{code_bg_start_x - pad, code_bg_y},
                               {end_x + pad, code_bg_y + code_bg_line_h}});
      code_bg_start_x = -1;
    }
  };

  for (auto& span : spans) {
    const char* p = span.text.data();
    const char* end = p + span.text.size();

    while (p < end) {
      while (p < end && *p == ' ') {
        x += space_w;
        p++;
        text_offset++;
      }
      if (p >= end)
        break;

      const char* word_start = p;
      while (p < end && *p != ' ')
        p++;
      const char* word_end = p;
      int word_len = static_cast<int>(word_end - word_start);
      int word_start_offset = text_offset;
      text_offset += word_len;

      ImVec2 word_size =
          font->CalcTextSizeA(font_size, FLT_MAX, 0, word_start, word_end);

      if (x + word_size.x > region_x && !first_on_line) {
        // Line wrap — flush code background for the previous line.
        if (span.style == SpanStyle::kCode)
          flush_code_bg(code_bg_end_x);
        x = 0;
        first_on_line = true;
      }

      if (!first_on_line) {
        ImGui::SameLine(start_x + x, 0);
      } else {
        ImGui::SetCursorPosX(start_x + x);
      }

      // Get screen position for highlight rects.
      ImVec2 screen_pos = ImGui::GetCursorScreenPos();
      float line_h = ImGui::GetTextLineHeight();

      // Draw highlight rectangles behind text.
      for (auto& hl : highlights) {
        int hl_end = hl.offset + hl.length;
        if (hl.offset < word_start_offset + word_len &&
            hl_end > word_start_offset) {
          int vis_start =
              std::max(hl.offset, word_start_offset) - word_start_offset;
          int vis_end = std::min(hl_end, word_start_offset + word_len) -
                        word_start_offset;
          float x1 = font->CalcTextSizeA(font_size, FLT_MAX, 0, word_start,
                                         word_start + vis_start)
                         .x;
          float x2 = font->CalcTextSizeA(font_size, FLT_MAX, 0, word_start,
                                         word_start + vis_end)
                         .x;
          ImU32 color = hl.is_current ? GetSyntaxPalette().search_current_bg
                                      : GetSyntaxPalette().search_match_bg;
          dl->AddRectFilled(ImVec2(screen_pos.x + x1, screen_pos.y),
                            ImVec2(screen_pos.x + x2, screen_pos.y + line_h),
                            color);
        }
      }

      // Draw selection highlight.
      if (sel_c1 >= 0 && sel_c2 > sel_c1 &&
          word_start_offset + word_len > sel_c1 && word_start_offset < sel_c2) {
        int vs = std::max(sel_c1, word_start_offset) - word_start_offset;
        int ve =
            std::min(sel_c2, word_start_offset + word_len) - word_start_offset;
        float sx1 = font->CalcTextSizeA(font_size, FLT_MAX, 0, word_start,
                                        word_start + vs)
                        .x;
        float sx2 = font->CalcTextSizeA(font_size, FLT_MAX, 0, word_start,
                                        word_start + ve)
                        .x;
        dl->AddRectFilled(ImVec2(screen_pos.x + sx1, screen_pos.y),
                          ImVec2(screen_pos.x + sx2, screen_pos.y + line_h),
                          ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
      }

      // Record text geometry for hit-testing.
      if (text_rects) {
        text_rects->push_back({screen_pos.x, screen_pos.y, word_size.x, line_h,
                               font_scale, block_index, word_start_offset,
                               word_start_offset + word_len});
      }

      // Render text.
      if (span.style == SpanStyle::kCode) {
        if (code_bg_start_x < 0) {
          code_bg_start_x = screen_pos.x;
          code_bg_y = screen_pos.y;
          code_bg_line_h = line_h;
        }
        ImGui::TextUnformatted(word_start, word_end);
      } else if (span.style == SpanStyle::kLink) {
        ImVec4 link_color = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
        ImGui::PushStyleColor(ImGuiCol_Text, link_color);
        ImGui::TextUnformatted(word_start, word_end);
        ImGui::PopStyleColor();
        // Extend underline and hit area to cover trailing space between words.
        float hit_right = screen_pos.x + word_size.x;
        if (p < end)
          hit_right += space_w;
        // Underline the link text.
        float underline_y = screen_pos.y + line_h - 1.0f;
        dl->AddLine(ImVec2(screen_pos.x, underline_y),
                    ImVec2(hit_right, underline_y),
                    ImGui::ColorConvertFloat4ToU32(link_color));
        // Show hand cursor when hovering a link.
        {
          ImVec2 mouse = ImGui::GetMousePos();
          if (mouse.x >= screen_pos.x && mouse.x <= hit_right &&
              mouse.y >= screen_pos.y && mouse.y <= screen_pos.y + line_h) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          }
        }
        // Detect link activation on mouse release without drag.
        if (clicked_link && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
          ImVec2 mouse = ImGui::GetMousePos();
          if (mouse.x >= screen_pos.x && mouse.x <= hit_right &&
              mouse.y >= screen_pos.y && mouse.y <= screen_pos.y + line_h) {
            *clicked_link = std::string(span.link_target);
          }
        }
      } else {
        ImGui::TextUnformatted(word_start, word_end);
        if (span.style == SpanStyle::kBold) {
          ImGui::SameLine(start_x + x + 1.0f, 0);
          ImGui::TextUnformatted(word_start, word_end);
        }
      }

      // Scroll to match if this word contains the target offset.
      if (!scrolled && scroll_to_offset >= 0 &&
          scroll_to_offset >= word_start_offset &&
          scroll_to_offset < word_start_offset + word_len) {
        ImGui::SetScrollHereY(0.5f);
        scrolled = true;
      }

      // Track rightmost edge for code background.
      if (span.style == SpanStyle::kCode)
        code_bg_end_x = screen_pos.x + word_size.x;

      x += word_size.x;
      first_on_line = false;
    }
    if (span.style == SpanStyle::kCode)
      flush_code_bg(code_bg_end_x);
  }

  // Draw inline code backgrounds behind text.
  if (has_code) {
    dl->ChannelsSetCurrent(0);  // Background channel.
    if (!code_bg_rects.empty()) {
      ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
      bg.w = 1.0f;
      ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(bg);
      float rounding = ImGui::GetStyle().FrameRounding;
      for (auto& r : code_bg_rects)
        dl->AddRectFilled(r.min, r.max, bg_col, rounding);
    }
    dl->ChannelsMerge();
  }
}

}  // namespace

void MarkdownRenderer::Parse(const char* data, size_t len) {
  blocks_.clear();
  const char* p = data;
  const char* end = data + len;
  bool in_code_block = false;
  std::string code_accum;

  while (p < end) {
    const char* line_end = static_cast<const char*>(memchr(p, '\n', end - p));
    if (!line_end)
      line_end = end;
    std::string_view line(p, line_end - p);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    p = line_end + 1;

    if (line.starts_with("```")) {
      if (in_code_block) {
        blocks_.push_back({Block::kCodeBlock, std::move(code_accum), 0, {}});
        code_accum.clear();
      }
      in_code_block = !in_code_block;
      continue;
    }

    if (in_code_block) {
      if (!code_accum.empty())
        code_accum += '\n';
      code_accum.append(line);
      continue;
    }

    if (line.empty()) {
      blocks_.push_back({Block::kEmpty, {}, 0, {}});
      continue;
    }

    if (line.starts_with("#### ")) {
      blocks_.push_back({Block::kHeading, std::string(line.substr(5)), 4, {}});
      continue;
    }
    if (line.starts_with("### ")) {
      blocks_.push_back({Block::kHeading, std::string(line.substr(4)), 3, {}});
      continue;
    }
    if (line.starts_with("## ")) {
      blocks_.push_back({Block::kHeading, std::string(line.substr(3)), 2, {}});
      continue;
    }
    if (line.starts_with("# ")) {
      blocks_.push_back({Block::kHeading, std::string(line.substr(2)), 1, {}});
      continue;
    }

    if (line.starts_with("  - ")) {
      blocks_.push_back({Block::kList, std::string(line.substr(4)), 2, {}});
      continue;
    }
    if (line.starts_with("- ")) {
      blocks_.push_back({Block::kList, std::string(line.substr(2)), 1, {}});
      continue;
    }

    // Paragraph text: join continuation lines.
    if (!blocks_.empty() && blocks_.back().type == Block::kParagraph) {
      blocks_.back().text += ' ';
      blocks_.back().text.append(line);
    } else if (!blocks_.empty() && blocks_.back().type == Block::kList) {
      blocks_.back().text += ' ';
      size_t trim = line.find_first_not_of(' ');
      if (trim != std::string_view::npos)
        blocks_.back().text.append(line.substr(trim));
      else
        blocks_.back().text.append(line);
    } else {
      blocks_.push_back({Block::kParagraph, std::string(line), 0, {}});
    }
  }

  // Build search_text for each block.
  for (auto& block : blocks_) {
    switch (block.type) {
      case Block::kCodeBlock:
        block.search_text = block.text;
        break;
      case Block::kEmpty:
        break;
      default:
        block.search_text = BuildSearchText(block.text);
        break;
    }
  }

  parsed_ = true;
}

bool MarkdownRenderer::RenderBlocks() {
  // --- Mouse input (uses text_rects_ from previous frame) ---
  bool hovered =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    int block, ch;
    MouseToPos(ImGui::GetMousePos().x, ImGui::GetMousePos().y, block, ch);
    if (block >= 0) {
      sel_start_block_ = sel_end_block_ = block;
      sel_start_char_ = sel_end_char_ = ch;
      sel_dragging_ = true;
    }
  }

  if (sel_dragging_) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      int block, ch;
      MouseToPos(ImGui::GetMousePos().x, ImGui::GetMousePos().y, block, ch);
      if (block >= 0) {
        sel_end_block_ = block;
        sel_end_char_ = ch;
      }
    } else {
      sel_dragging_ = false;
    }
  }

  // Set text cursor when hovering over text (uses previous frame's rects).
  if (hovered && !text_rects_.empty()) {
    ImVec2 mouse = ImGui::GetMousePos();
    for (auto& r : text_rects_) {
      if (mouse.y >= r.y && mouse.y <= r.y + r.h) {
        // Mouse is on this line. Find the horizontal extent of the line.
        float line_min_x = r.x;
        float line_max_x = r.x + r.w;
        for (auto& r2 : text_rects_) {
          if (std::abs(r2.y - r.y) < r.h * 0.5f) {
            line_min_x = std::min(line_min_x, r2.x);
            line_max_x = std::max(line_max_x, r2.x + r2.w);
          }
        }
        if (mouse.x >= line_min_x && mouse.x <= line_max_x)
          ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        break;
      }
    }
  }

  // Clear text_rects_ for this frame.
  text_rects_.clear();

  // Normalize selection for rendering.
  int s_block = sel_start_block_, s_char = sel_start_char_;
  int e_block = sel_end_block_, e_char = sel_end_char_;
  if (s_block > e_block || (s_block == e_block && s_char > e_char)) {
    std::swap(s_block, e_block);
    std::swap(s_char, e_char);
  }
  bool has_selection = s_block >= 0 && (s_block != e_block || s_char != e_char);

  // --- Render blocks ---
  float indent_base = ImGui::GetCursorPosX();
  bool scrolled = false;
  heading_positions_.clear();
  std::string clicked_link;

  for (int bi = 0; bi < static_cast<int>(blocks_.size()); bi++) {
    auto& block = blocks_[bi];

    // Build highlight regions for this block.
    std::vector<HighlightRegion> highlights;
    int scroll_offset = -1;
    if (search_active_ && !matches_.empty()) {
      for (int mi = 0; mi < static_cast<int>(matches_.size()); mi++) {
        if (matches_[mi].block == bi) {
          highlights.push_back(
              {matches_[mi].offset, matches_[mi].length, mi == current_match_});
        }
      }
      if (!scrolled && scroll_to_match_ >= 0 &&
          scroll_to_match_ < static_cast<int>(matches_.size()) &&
          matches_[scroll_to_match_].block == bi) {
        scroll_offset = matches_[scroll_to_match_].offset;
      }
    }

    // Compute per-block selection range.
    int sel_c1 = -1, sel_c2 = -1;
    if (has_selection && bi >= s_block && bi <= e_block) {
      sel_c1 = (bi == s_block) ? s_char : 0;
      sel_c2 =
          (bi == e_block) ? e_char : static_cast<int>(block.search_text.size());
    }

    switch (block.type) {
      case Block::kEmpty:
        ImGui::Spacing();
        break;

      case Block::kHeading: {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        if (block.level <= 2)
          heading_positions_.push_back({bi, ImGui::GetCursorPosY()});
        if (scroll_to_block_ == bi) {
          ImGui::SetScrollHereY(0.0f);
          scroll_to_block_ = -1;
        }
        float scale = block.level == 1   ? 2.0f
                      : block.level == 2 ? 1.5f
                      : block.level == 3 ? 1.25f
                                         : 1.1f;
        ImGui::SetWindowFontScale(scale);
        auto spans = ParseInlineSpans(block.text);
        RenderWrappedSpans(spans, highlights, scroll_offset, scrolled,
                           &text_rects_, bi, scale, sel_c1, sel_c2,
                           &clicked_link);
        if (block.level <= 2)
          ImGui::Separator();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        break;
      }

      case Block::kList: {
        float indent = ImGui::GetFontSize() * (block.level == 2 ? 2.0f : 1.0f);
        ImGui::SetCursorPosX(indent_base + indent);
        ImGui::TextUnformatted("\xe2\x80\xa2");
        ImGui::SameLine();
        auto spans = ParseInlineSpans(block.text);
        RenderWrappedSpans(spans, highlights, scroll_offset, scrolled,
                           &text_rects_, bi, 1.0f, sel_c1, sel_c2,
                           &clicked_link);
        break;
      }

      case Block::kCodeBlock: {
        float indent = ImGui::GetFontSize() * 1.0f;
        float padding = ImGui::GetFontSize() * 0.2f;
        const char* cp = block.text.c_str();
        const char* ce = cp + block.text.size();
        int line_offset = 0;
        ImFont* font = ImGui::GetFont();
        float font_size = ImGui::GetFontSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float line_h = ImGui::GetTextLineHeight();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        ImVec2 code_block_min = ImGui::GetCursorScreenPos();
        code_block_min.x = ImGui::GetWindowPos().x;
        code_block_min.y -= padding;
        float max_content_right = 0;
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);  // Foreground for text/highlights.
        while (cp < ce) {
          const char* nl = static_cast<const char*>(memchr(cp, '\n', ce - cp));
          if (!nl)
            nl = ce;
          int line_len = static_cast<int>(nl - cp);
          ImGui::SetCursorPosX(indent_base + indent);

          // Draw highlight rects for this code line.
          ImVec2 screen_pos = ImGui::GetCursorScreenPos();
          for (auto& hl : highlights) {
            int hl_end = hl.offset + hl.length;
            if (hl.offset < line_offset + line_len && hl_end > line_offset) {
              int vis_start = std::max(hl.offset, line_offset) - line_offset;
              int vis_end =
                  std::min(hl_end, line_offset + line_len) - line_offset;
              float x1 =
                  font->CalcTextSizeA(font_size, FLT_MAX, 0, cp, cp + vis_start)
                      .x;
              float x2 =
                  font->CalcTextSizeA(font_size, FLT_MAX, 0, cp, cp + vis_end)
                      .x;
              ImU32 color = hl.is_current ? GetSyntaxPalette().search_current_bg
                                          : GetSyntaxPalette().search_match_bg;
              dl->AddRectFilled(
                  ImVec2(screen_pos.x + x1, screen_pos.y),
                  ImVec2(screen_pos.x + x2, screen_pos.y + line_h), color);
            }
          }

          // Draw selection highlight for this code line.
          if (sel_c1 >= 0 && sel_c2 > sel_c1 &&
              line_offset + line_len > sel_c1 && line_offset < sel_c2) {
            int vs = std::max(sel_c1, line_offset) - line_offset;
            int ve = std::min(sel_c2, line_offset + line_len) - line_offset;
            float sx1 =
                font->CalcTextSizeA(font_size, FLT_MAX, 0, cp, cp + vs).x;
            float sx2 =
                font->CalcTextSizeA(font_size, FLT_MAX, 0, cp, cp + ve).x;
            dl->AddRectFilled(ImVec2(screen_pos.x + sx1, screen_pos.y),
                              ImVec2(screen_pos.x + sx2, screen_pos.y + line_h),
                              ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
          }

          // Record text geometry for hit-testing.
          if (line_len > 0) {
            float lw =
                font->CalcTextSizeA(font_size, FLT_MAX, 0, cp, cp + line_len).x;
            max_content_right = std::max(max_content_right, screen_pos.x + lw);
            text_rects_.push_back({screen_pos.x, screen_pos.y, lw, line_h, 1.0f,
                                   bi, line_offset, line_offset + line_len});
          }

          if (nl > cp)
            ImGui::TextUnformatted(cp, nl);
          else
            ImGui::TextUnformatted("");

          // Scroll to match in this code line.
          if (!scrolled && scroll_offset >= 0 && scroll_offset >= line_offset &&
              scroll_offset < line_offset + line_len) {
            ImGui::SetScrollHereY(0.5f);
            scrolled = true;
          }

          line_offset += line_len + 1;  // +1 for newline
          cp = nl + 1;
        }
        // Draw background rectangle behind the code block.
        dl->ChannelsSetCurrent(0);  // Background channel.
        {
          ImGuiWindow* win = ImGui::GetCurrentWindow();
          float content_right = win->Pos.x - ImGui::GetScrollX() +
                                win->ContentSize.x + win->WindowPadding.x;
          float visible_right =
              ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
          float right = std::max(
              {visible_right, content_right, max_content_right + padding});
          ImVec2 code_block_max(right, ImGui::GetCursorScreenPos().y -
                                           ImGui::GetStyle().ItemSpacing.y +
                                           padding);
          ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
          bg.w = 1.0f;
          dl->AddRectFilled(code_block_min, code_block_max,
                            ImGui::ColorConvertFloat4ToU32(bg),
                            ImGui::GetStyle().FrameRounding);
        }
        dl->ChannelsMerge();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding);
        ImGui::Dummy({0, 0});
        break;
      }

      case Block::kParagraph: {
        auto spans = ParseInlineSpans(block.text);
        RenderWrappedSpans(spans, highlights, scroll_offset, scrolled,
                           &text_rects_, bi, 1.0f, sel_c1, sel_c2,
                           &clicked_link);
        break;
      }
    }
  }

  if (scrolled)
    scroll_to_match_ = -1;

  // Handle clicked links (only if no text was selected by dragging).
  bool link_activated = false;
  if (!clicked_link.empty() && s_block == e_block && s_char == e_char) {
    if (clicked_link[0] == '#') {
      std::string anchor = clicked_link.substr(1);
      for (int bi = 0; bi < static_cast<int>(blocks_.size()); bi++) {
        if (blocks_[bi].type == Block::kHeading &&
            HeadingToSlug(blocks_[bi].text) == anchor) {
          scroll_to_block_ = bi;
          link_activated = true;
          break;
        }
      }
    } else if (clicked_link.starts_with("http://") ||
               clicked_link.starts_with("https://")) {
      auto& pio = ImGui::GetPlatformIO();
      if (pio.Platform_OpenInShellFn)
        pio.Platform_OpenInShellFn(ImGui::GetCurrentContext(),
                                   clicked_link.c_str());
      link_activated = true;
    }
  }

  // --- Keyboard shortcuts ---
  if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
      sel_start_block_ = 0;
      sel_start_char_ = 0;
      for (int i = static_cast<int>(blocks_.size()) - 1; i >= 0; --i) {
        if (!blocks_[i].search_text.empty()) {
          sel_end_block_ = i;
          sel_end_char_ = static_cast<int>(blocks_[i].search_text.size());
          break;
        }
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C, false) && has_selection) {
      std::string text = GetSelectedText();
      if (!text.empty())
        ImGui::SetClipboardText(text.c_str());
    }
  }

  // --- Context menu ---
  context_menu_open_ = false;
  if (ImGui::BeginPopupContextWindow()) {
    context_menu_open_ = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##md_ctx_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Select all", "Ctrl+A")) {
      sel_start_block_ = 0;
      sel_start_char_ = 0;
      for (int i = static_cast<int>(blocks_.size()) - 1; i >= 0; --i) {
        if (!blocks_[i].search_text.empty()) {
          sel_end_block_ = i;
          sel_end_char_ = static_cast<int>(blocks_[i].search_text.size());
          break;
        }
      }
    }
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_selection)) {
      std::string text = GetSelectedText();
      if (!text.empty())
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::EndPopup();
  }

  return link_activated;
}

void MarkdownRenderer::UpdateSearchBar() {
  SearchBar::Options opts;
  opts.id = "md_search";
  opts.match_counter = true;
  opts.close_button = true;
  opts.matches = {current_match_, static_cast<int>(matches_.size()), true};
  SearchBar::Result search = search_bar_.Update(opts, &search_options_);

  // Re-run the search when the matches went stale. Compared against what they
  // were built from rather than driven by |search|, so a document shown again
  // after an option was toggled on another one refreshes too.
  base::SearchTerm needle(search_bar_.term(), search_options_);
  if (needle != last_needle_) {
    FindMatches(needle);
    last_needle_ = std::move(needle);
  }

  int count = static_cast<int>(matches_.size());
  if (count > 0 && (search.next || search.prev)) {
    int next;
    if (search.next)
      next = current_match_ < 0 ? 0 : (current_match_ + 1) % count;
    else
      next =
          current_match_ < 0 ? count - 1 : (current_match_ - 1 + count) % count;
    SetCurrentMatch(next);
  }

  // Escape closes the bar. Skipped while the content's context menu is open so
  // that its own Escape handler runs first.
  if (search.close_requested ||
      (!context_menu_open_ && ImGui::IsKeyPressed(ImGuiKey_Escape)))
    search_active_ = false;
}

bool MarkdownRenderer::Render(const char* id, ImVec2 size) {
  // Ctrl+F opens the search bar and focuses it.
  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    search_active_ = true;
    search_bar_.Focus();
  }

  if (search_active_)
    UpdateSearchBar();

  ImGui::BeginChild(id, size, ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);
  UpdateScroll();
  bool link_activated = RenderBlocks();
  ImGui::EndChild();
  return link_activated;
}

void MarkdownRenderer::UpdateScroll() {
  float scroll_y = ImGui::GetScrollY();

  // Heading jumps queued by the host's buttons. Not gated on keyboard focus:
  // a click is explicit, unlike the shortcuts below.
  if (scroll_heading_dir_ != 0) {
    if (scroll_heading_dir_ < 0)
      ScrollToPrevHeading(scroll_y);
    else
      ScrollToNextHeading(scroll_y);
    scroll_heading_dir_ = 0;
  }

  if (ImGui::GetIO().WantTextInput)
    return;

  float max_scroll = ImGui::GetScrollMaxY();
  float line_h = ImGui::GetTextLineHeightWithSpacing();
  float page = std::max(line_h, ImGui::GetWindowHeight() - line_h);
  if (ImGui::IsKeyPressed(ImGuiKey_Tab) && ImGui::GetIO().KeyShift)
    ScrollToPrevHeading(scroll_y);
  else if (ImGui::IsKeyPressed(ImGuiKey_Tab))
    ScrollToNextHeading(scroll_y);
  else if (ImGui::IsKeyPressed(ImGuiKey_Home))
    ImGui::SetScrollY(0);
  else if (ImGui::IsKeyPressed(ImGuiKey_End))
    ImGui::SetScrollY(max_scroll);
  else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
    ImGui::SetScrollY(std::max(0.0f, scroll_y - page));
  else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
    ImGui::SetScrollY(std::min(max_scroll, scroll_y + page));
  else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    ImGui::SetScrollY(std::max(0.0f, scroll_y - line_h));
  else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    ImGui::SetScrollY(std::min(max_scroll, scroll_y + line_h));
}

void MarkdownRenderer::FindMatches(const base::SearchTerm& needle) {
  matches_.clear();
  current_match_ = -1;
  scroll_to_match_ = -1;

  if (needle.empty())
    return;

  int length = static_cast<int>(needle.size());

  for (int bi = 0; bi < static_cast<int>(blocks_.size()); bi++) {
    const std::string& source = blocks_[bi].search_text;
    if (source.empty())
      continue;
    base::ForEachMatch(source, needle, match_buf_, [&](size_t pos) {
      matches_.push_back({bi, static_cast<int>(pos), length});
    });
  }
}

void MarkdownRenderer::SetCurrentMatch(int index) {
  if (index >= 0 && index < static_cast<int>(matches_.size())) {
    current_match_ = index;
    scroll_to_match_ = index;
  }
}

void MarkdownRenderer::ScrollToNextHeading(float current_scroll_y) {
  float threshold = ImGui::GetTextLineHeightWithSpacing();
  for (auto& hp : heading_positions_) {
    if (hp.y > current_scroll_y + threshold) {
      scroll_to_block_ = hp.block_index;
      return;
    }
  }
}

void MarkdownRenderer::ScrollToPrevHeading(float current_scroll_y) {
  float threshold = ImGui::GetTextLineHeightWithSpacing();
  for (int i = static_cast<int>(heading_positions_.size()) - 1; i >= 0; i--) {
    if (heading_positions_[i].y < current_scroll_y - threshold) {
      scroll_to_block_ = heading_positions_[i].block_index;
      return;
    }
  }
}

void MarkdownRenderer::MouseToPos(float mx,
                                  float my,
                                  int& block,
                                  int& char_offset) const {
  block = -1;
  char_offset = 0;
  if (text_rects_.empty())
    return;

  // Find the text rect line closest to the mouse Y.
  float best_y_dist = FLT_MAX;
  float best_y = 0;
  for (auto& r : text_rects_) {
    float dist = (my < r.y)         ? r.y - my
                 : (my > r.y + r.h) ? my - (r.y + r.h)
                                    : 0.0f;
    if (dist < best_y_dist) {
      best_y_dist = dist;
      best_y = r.y;
    }
  }

  // Among rects on that line, find the one closest to mouse X.
  const TextRect* best = nullptr;
  float best_x_dist = FLT_MAX;
  for (auto& r : text_rects_) {
    if (std::abs(r.y - best_y) > r.h * 0.5f)
      continue;
    float dist = (mx < r.x)         ? r.x - mx
                 : (mx > r.x + r.w) ? mx - (r.x + r.w)
                                    : 0.0f;
    if (dist < best_x_dist) {
      best_x_dist = dist;
      best = &r;
    }
  }

  if (!best)
    return;

  block = best->block;

  // Compute character offset within the rect.
  float local_x = mx - best->x;
  if (local_x <= 0) {
    char_offset = best->char_start;
    return;
  }
  if (local_x >= best->w) {
    char_offset = best->char_end;
    return;
  }

  const std::string& st = blocks_[block].search_text;
  ImFont* font = ImGui::GetFont();
  float font_size = ImGui::GetFontSize() * best->font_scale;
  const char* base = st.c_str() + best->char_start;
  int len = best->char_end - best->char_start;
  char_offset = best->char_end;
  for (int c = 1;
       c <= len && best->char_start + c <= static_cast<int>(st.size()); c++) {
    float w = font->CalcTextSizeA(font_size, FLT_MAX, 0, base, base + c).x;
    if (w > local_x) {
      float w_prev =
          (c > 1)
              ? font->CalcTextSizeA(font_size, FLT_MAX, 0, base, base + c - 1).x
              : 0;
      char_offset =
          best->char_start + ((local_x - w_prev < w - local_x) ? c - 1 : c);
      break;
    }
  }
}

std::string MarkdownRenderer::GetSelectedText() const {
  int sb = sel_start_block_, sc = sel_start_char_;
  int eb = sel_end_block_, ec = sel_end_char_;
  if (sb > eb || (sb == eb && sc > ec)) {
    std::swap(sb, eb);
    std::swap(sc, ec);
  }
  if (sb < 0)
    return {};

  std::string text;
  for (int i = sb; i <= eb && i < static_cast<int>(blocks_.size()); i++) {
    const auto& block = blocks_[i];
    if (i > sb && !text.empty())
      text += '\n';
    if (block.type == Block::kEmpty)
      continue;
    const std::string& st = block.search_text;
    int c1 = (i == sb) ? std::min(sc, static_cast<int>(st.size())) : 0;
    int c2 = (i == eb) ? std::min(ec, static_cast<int>(st.size()))
                       : static_cast<int>(st.size());
    if (c2 > c1)
      text += st.substr(c1, c2 - c1);
  }
  return text;
}
