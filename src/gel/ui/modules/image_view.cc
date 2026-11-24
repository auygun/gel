// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/image_view.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

#include "base/exec.h"
#include "third_party/kaliber/base/thread_pool.h"
#include "third_party/stb/stb_image.h"

namespace {

// Larger files aren't read, and larger images aren't decoded: an image of the
// maximum size takes 256 MB decoded. Each side also has to fit in a texture.
constexpr size_t kMaxFileBytes = size_t{256} * 1024 * 1024;
constexpr int64_t kMaxPixels = int64_t{8192} * 8192;
constexpr int kMaxSide = 16384;

// Side of the squares of the checkerboard behind transparent pixels.
constexpr float kCheckerSize = 16.0f;

}  // namespace

bool IsImageFile(const std::string& path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
         ext == ".gif" || ext == ".tga" || ext == ".psd";
}

ImageView::ImageView(GraphicsDelegate& graphics_delegate,
                     std::function<void(bool)> busy_callback)
    : graphics_delegate_(graphics_delegate),
      busy_callback_(std::move(busy_callback)),
      latest_(std::make_shared<std::atomic<int>>(0)) {}

ImageView::~ImageView() {
  // Loads in progress stop early, and their replies find themselves
  // superseded and don't touch this object.
  Clear();
}

void ImageView::LoadBlob(const std::string& blob,
                         const std::filesystem::path& toplevel) {
  request_ = {false, toplevel, blob};
  Load();
}

void ImageView::LoadFile(const std::filesystem::path& path) {
  request_ = {true, path, {}};
  Load();
}

void ImageView::Clear() {
  latest_->fetch_add(1, std::memory_order_relaxed);
  if (state_ == State::kLoading)
    busy_callback_(false);
  state_ = State::kEmpty;
  width_ = 0;
  height_ = 0;
  has_alpha_ = false;
  if (image_ != GraphicsDelegate::kNoImage)
    graphics_delegate_.DestroyImage(image_);
  image_ = GraphicsDelegate::kNoImage;
}

void ImageView::Load() {
  Clear();
  state_ = State::kLoading;
  center_scroll_ = true;
  busy_callback_(true);
  int generation = latest_->fetch_add(1, std::memory_order_relaxed) + 1;
  base::ThreadPool::Get().PostTaskAndReplyWithResult<Image>(
      HERE,
      [request = request_, latest = latest_, generation]() {
        return ReadImage(request, *latest, generation);
      },
      [this, latest = latest_, generation](Image image) {
        // Superseded, or this object is gone.
        if (latest->load(std::memory_order_relaxed) != generation)
          return;
        busy_callback_(false);
        state_ = image.state;
        width_ = image.width;
        height_ = image.height;
        has_alpha_ = image.has_alpha;
        if (state_ == State::kLoaded) {
          image_ = graphics_delegate_.CreateImage(width_, height_,
                                                  std::move(image.pixels));
        }
      });
}

void ImageView::SetScaleMode(ScaleMode mode) {
  if (mode == scale_mode_)
    return;
  scale_mode_ = mode;
  center_scroll_ = true;
}

void ImageView::Render(float width) {
  bool full_size = scale_mode_ == ScaleMode::kFullSize;
  ImGuiWindowFlags flags = full_size ? ImGuiWindowFlags_HorizontalScrollbar
                                     : ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::BeginChild("image_view", ImVec2(width, -FLT_MIN),
                        ImGuiChildFlags_Borders, flags)) {
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();
    content_top_right_ =
        ImVec2(wpos.x + wsz.x - ImGui::GetStyle().ScrollbarSize, wpos.y);

    if (state_ == State::kLoaded && image_ != GraphicsDelegate::kNoImage &&
        width_ > 0 && height_ > 0) {
      // The visible content region, less any scrollbars, wherever it's
      // scrolled to.
      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 origin = ImGui::GetCursorScreenPos();
      // Pixel for pixel, or scaled to fill keeping the aspect ratio.
      float scale = 1.0f;
      if (!full_size) {
        scale = std::max(std::min(avail.x / static_cast<float>(width_),
                                  avail.y / static_cast<float>(height_)),
                         0.0f);
      }
      float w = std::round(static_cast<float>(width_) * scale);
      float h = std::round(static_cast<float>(height_) * scale);
      // Centered where it's smaller than the view, otherwise at the start of
      // the scrollable area.
      ImVec2 offset(std::round(std::max((avail.x - w) / 2, 0.0f)),
                    std::round(std::max((avail.y - h) / 2, 0.0f)));
      ImVec2 min(origin.x + offset.x, origin.y + offset.y);
      ImVec2 max(min.x + w, min.y + h);

      // Sizes the scrollable area: the image, or the view if it's larger.
      ImGui::Dummy(full_size ? ImVec2(offset.x + w, offset.y + h) : avail);

      if (has_alpha_)
        DrawCheckerboard(min, max);
      if (!graphics_delegate_.DrawImage(image_, min, max)) {
        // Gone with the renderer: load it again.
        Load();
      } else if (center_scroll_) {
        // On the image's center, once it's shown full size. Takes effect
        // next frame.
        if (full_size) {
          ImGui::SetScrollX(std::max((w - avail.x) / 2, 0.0f));
          ImGui::SetScrollY(std::max((h - avail.y) / 2, 0.0f));
        }
        center_scroll_ = false;
      }
    }
  }
  ImGui::EndChild();
}

// static
void ImageView::DrawCheckerboard(const ImVec2& min, const ImVec2& max) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(min, max, true);
  // Only the squares within the clip rect, which for a large image shown full
  // size is a small part of it.
  ImVec2 clip_min = dl->GetClipRectMin();
  ImVec2 clip_max = dl->GetClipRectMax();
  dl->AddRectFilled(clip_min, clip_max, IM_COL32(204, 204, 204, 255));
  int first_row = std::max(
      static_cast<int>(std::floor((clip_min.y - min.y) / kCheckerSize)), 0);
  int last_row =
      static_cast<int>(std::ceil((clip_max.y - min.y) / kCheckerSize));
  int first_column = std::max(
      static_cast<int>(std::floor((clip_min.x - min.x) / kCheckerSize)), 0);
  int last_column =
      static_cast<int>(std::ceil((clip_max.x - min.x) / kCheckerSize));
  for (int row = first_row; row < last_row; row++) {
    float y = min.y + static_cast<float>(row) * kCheckerSize;
    // Every other square, starting a square later on odd rows.
    int column = first_column + ((first_column + row) % 2);
    for (; column < last_column; column += 2) {
      float x = min.x + static_cast<float>(column) * kCheckerSize;
      dl->AddRectFilled(ImVec2(x, y),
                        ImVec2(x + kCheckerSize, y + kCheckerSize),
                        IM_COL32(153, 153, 153, 255));
    }
  }
  dl->PopClipRect();
}

// static
ImageView::Image ImageView::ReadImage(const Request& request,
                                      const std::atomic<int>& latest,
                                      int generation) {
  auto superseded = [&] {
    return latest.load(std::memory_order_relaxed) != generation;
  };
  Image too_large;
  too_large.state = State::kTooLarge;
  std::string data;

  if (request.from_file) {
    std::error_code ec;
    auto size = std::filesystem::file_size(request.path, ec);
    if (ec)
      return {};
    if (size > kMaxFileBytes)
      return too_large;
    std::ifstream in(request.path, std::ios::binary);
    if (!in)
      return {};
    data.resize(static_cast<size_t>(size));
    // In chunks, to stop early once superseded.
    constexpr size_t kChunk = size_t{1024} * 1024;
    size_t read = 0;
    while (read < data.size()) {
      if (superseded())
        return {};
      size_t n = std::min(kChunk, data.size() - read);
      in.read(data.data() + read, static_cast<std::streamsize>(n));
      size_t got = static_cast<size_t>(in.gcount());
      read += got;
      if (got < n)
        break;
    }
    // The file shrank while read.
    data.resize(read);
  } else {
    std::vector<std::string> args = {"git"};
    if (!request.path.empty())
      args.insert(args.end(), {"-C", request.path.string()});
    args.insert(args.end(), {"cat-file", "blob", request.blob});
    Exec proc;
    if (!proc.Start(args))
      return {};
    bool killed = false;
    bool over_limit = false;
    while (proc.Poll()) {
      if (killed)
        continue;
      over_limit = proc.GetOut().size() > kMaxFileBytes;
      if (over_limit || superseded()) {
        // Reaped by polling on until it's gone.
        proc.Kill();
        killed = true;
      }
    }
    if (over_limit)
      return too_large;
    if (killed || proc.GetStatus() != Exec::Status::EXITED ||
        proc.GetResult() != 0)
      return {};
    data = std::move(proc.GetOut());
  }

  if (superseded())
    return {};
  return DecodeImage(data);
}

// static
ImageView::Image ImageView::DecodeImage(const std::string& data) {
  Image result;
  if (data.empty() || data.size() > kMaxFileBytes)
    return result;
  auto* bytes = reinterpret_cast<const stbi_uc*>(data.data());
  int len = static_cast<int>(data.size());

  // Its size first, from the header, to not decode one too large to show.
  int w = 0, h = 0, comp = 0;
  if (!stbi_info_from_memory(bytes, len, &w, &h, &comp) || w <= 0 || h <= 0)
    return result;
  if (w > kMaxSide || h > kMaxSide ||
      static_cast<int64_t>(w) * h > kMaxPixels) {
    result.state = State::kTooLarge;
    result.width = w;
    result.height = h;
    return result;
  }

  stbi_uc* pixels = stbi_load_from_memory(bytes, len, &w, &h, nullptr, 4);
  if (!pixels)
    return result;
  result.state = State::kLoaded;
  result.width = w;
  result.height = h;
  // Grey with alpha, or RGB with alpha.
  result.has_alpha = comp == 2 || comp == 4;
  result.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
  stbi_image_free(pixels);
  return result;
}
