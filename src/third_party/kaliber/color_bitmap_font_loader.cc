#include "third_party/kaliber/color_bitmap_font_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/base/file.h"
#include "third_party/kaliber/base/sfnt.h"
#include "third_party/stb/stb_image.h"

namespace eng {

using base::sfnt::FindTable;
using base::sfnt::FindTableRange;
using base::sfnt::Span;

namespace {

constexpr uint32_t kTagCblc = 0x43424C43;  // 'CBLC'
constexpr uint32_t kTagCbdt = 0x43424454;  // 'CBDT'
constexpr uint32_t kTagSbix = 0x73626978;  // 'sbix'
constexpr uint32_t kTagPng = 0x706E6720;   // 'png '
constexpr uint32_t kTagDupe = 0x64757065;  // 'dupe'

enum class StrikeKind { kCblc, kSbix };

struct Strike {
  int ppem_x = 0;
  int ppem_y = 0;
  // Baseline-to-baseline height of the strike, in strike pixels. Bitmaps are
  // scaled so that this maps onto the baked font size, which keeps emoji
  // inside the text line box. Color emoji bitmaps are noticeably taller than
  // their ppem (Noto's 109 ppem strike is 128 pixels tall), so scaling by ppem
  // instead would make them overlap neighbouring lines.
  int line_height = 0;
  size_t offset = 0;  // Into 'CBLC' (bitmapSizeTable) or 'sbix' (strike).
};

// A glyph's PNG payload plus its placement, in strike pixels.
struct GlyphBitmap {
  Span png;
  float bearing_x = 0.0f;  // Left side bearing.
  float bearing_y = 0.0f;  // Baseline to top of bitmap.
  float advance = 0.0f;
};

// Stored in ImFontConfig::FontLoaderData.
struct SrcData {
  FT_Face face = nullptr;
  StrikeKind kind = StrikeKind::kCblc;
  Span cblc;
  Span cbdt;
  Span sbix;
  std::vector<Strike> strikes;
  float units_per_em = 0.0f;
  int num_glyphs = 0;
};

// Stored in ImFontBaked::FontLoaderDatas (allocated by ImGui).
struct BakedData {
  int strike_index = 0;
  float scale = 1.0f;    // Strike pixels -> rasterized pixels.
  float density = 1.0f;  // Rasterized pixels -> layout pixels is 1/density.
};

//------------------------------------------------------------------------------
// Shared FreeType library
//
// Only used for the character map (and, for 'sbix', font-unit advances). ImGui
// forbids LoaderInit/LoaderShutdown on per-source loaders, so the library is
// refcounted by the sources instead. Font atlas building is main-thread only,
// so no locking is needed.

FT_Library g_ft_library = nullptr;
int g_ft_ref_count = 0;

bool AcquireFreeType() {
  if (g_ft_ref_count == 0 && FT_Init_FreeType(&g_ft_library) != 0) {
    g_ft_library = nullptr;
    return false;
  }
  ++g_ft_ref_count;
  return true;
}

void ReleaseFreeType() {
  if (--g_ft_ref_count == 0) {
    FT_Done_FreeType(g_ft_library);
    g_ft_library = nullptr;
  }
}

//------------------------------------------------------------------------------
// 'CBLC' / 'CBDT' (Noto Color Emoji)

void ParseCblcStrikes(Span cblc, std::vector<Strike>* strikes) {
  size_t num_sizes = cblc.U32(4);
  for (size_t i = 0; i < num_sizes; ++i) {
    size_t offset = 8 + 48 * i;
    if (!cblc.Has(offset, 48))
      break;
    Strike strike;
    strike.ppem_x = cblc.U8(offset + 44);
    strike.ppem_y = cblc.U8(offset + 45);
    // hori sbitLineMetrics: ascender then descender, both int8.
    strike.line_height = cblc.S8(offset + 16) - cblc.S8(offset + 17);
    if (strike.line_height <= 0)
      strike.line_height = strike.ppem_y;
    strike.offset = offset;
    if (strike.ppem_y > 0)
      strikes->push_back(strike);
  }
}

// Walks indexSubTableArray -> indexSubTable to find the 'CBDT' record for a
// glyph. Returns the record slice and, for formats that carry them there, the
// glyph metrics.
bool FindCbdtRecord(Span cblc,
                    Span cbdt,
                    const Strike& strike,
                    uint32_t glyph_index,
                    Span* out_record,
                    uint16_t* out_image_format,
                    GlyphBitmap* out_metrics) {
  size_t size_table = strike.offset;
  size_t array_offset = cblc.U32(size_table);
  size_t num_sub_tables = cblc.U32(size_table + 8);

  for (size_t i = 0; i < num_sub_tables; ++i) {
    size_t entry = array_offset + 8 * i;
    if (!cblc.Has(entry, 8))
      return false;
    size_t first = cblc.U16(entry);
    size_t last = cblc.U16(entry + 2);
    if (glyph_index < first || glyph_index > last)
      continue;

    size_t header = array_offset + cblc.U32(entry + 4);
    uint16_t index_format = cblc.U16(header);
    uint16_t image_format = cblc.U16(header + 2);
    size_t image_data_offset = cblc.U32(header + 4);
    size_t body = header + 8;
    size_t index = glyph_index - first;

    size_t data_offset = 0;
    size_t data_size = 0;
    switch (index_format) {
      case 1: {  // uint32 offsets, variable size.
        size_t begin = cblc.U32(body + 4 * index);
        size_t end = cblc.U32(body + 4 * (index + 1));
        if (end <= begin)
          return false;
        data_offset = image_data_offset + begin;
        data_size = end - begin;
        break;
      }
      case 2: {  // Constant size, metrics in the index sub-table.
        size_t image_size = cblc.U32(body);
        data_offset = image_data_offset + index * image_size;
        data_size = image_size;
        out_metrics->bearing_x = cblc.S8(body + 6);
        out_metrics->bearing_y = cblc.S8(body + 7);
        out_metrics->advance = cblc.U8(body + 8);
        break;
      }
      case 3: {  // uint16 offsets, variable size.
        size_t begin = cblc.U16(body + 2 * index);
        size_t end = cblc.U16(body + 2 * (index + 1));
        if (end <= begin)
          return false;
        data_offset = image_data_offset + begin;
        data_size = end - begin;
        break;
      }
      case 4: {  // Sparse, uint16 offsets.
        size_t num_glyphs = cblc.U32(body);
        size_t pairs = body + 4;
        bool found = false;
        for (size_t g = 0; g < num_glyphs; ++g) {
          if (!cblc.Has(pairs + 4 * g, 8))
            return false;
          if (cblc.U16(pairs + 4 * g) != glyph_index)
            continue;
          size_t begin = cblc.U16(pairs + 4 * g + 2);
          size_t end = cblc.U16(pairs + 4 * g + 6);
          if (end <= begin)
            return false;
          data_offset = image_data_offset + begin;
          data_size = end - begin;
          found = true;
          break;
        }
        if (!found)
          return false;
        break;
      }
      case 5: {  // Sparse, constant size.
        size_t image_size = cblc.U32(body);
        size_t num_glyphs = cblc.U32(body + 12);
        size_t codes = body + 16;
        bool found = false;
        for (size_t g = 0; g < num_glyphs; ++g) {
          if (!cblc.Has(codes + 2 * g, 2))
            return false;
          if (cblc.U16(codes + 2 * g) != glyph_index)
            continue;
          data_offset = image_data_offset + g * image_size;
          data_size = image_size;
          found = true;
          break;
        }
        if (!found)
          return false;
        out_metrics->bearing_x = cblc.S8(body + 6);
        out_metrics->bearing_y = cblc.S8(body + 7);
        out_metrics->advance = cblc.U8(body + 8);
        break;
      }
      default:
        return false;
    }

    Span record = cbdt.Sub(data_offset, data_size);
    if (record.empty())
      return false;
    *out_record = record;
    *out_image_format = image_format;
    return true;
  }
  return false;
}

bool LoadCbdtGlyph(const SrcData& src,
                   const Strike& strike,
                   uint32_t glyph_index,
                   GlyphBitmap* out) {
  Span record;
  uint16_t image_format = 0;
  if (!FindCbdtRecord(src.cblc, src.cbdt, strike, glyph_index, &record,
                      &image_format, out))
    return false;

  switch (image_format) {
    case 17:  // smallGlyphMetrics + uint32 length + PNG.
      out->bearing_x = record.S8(2);
      out->bearing_y = record.S8(3);
      out->advance = record.U8(4);
      out->png = record.Sub(9, record.U32(5));
      break;
    case 18:  // bigGlyphMetrics + uint32 length + PNG.
      out->bearing_x = record.S8(2);
      out->bearing_y = record.S8(3);
      out->advance = record.U8(4);
      out->png = record.Sub(12, record.U32(8));
      break;
    case 19:  // PNG only; metrics came from the index sub-table.
      out->png = record;
      break;
    default:
      return false;  // Formats 1-9 are monochrome/grayscale, not color.
  }
  return !out->png.empty();
}

//------------------------------------------------------------------------------
// 'sbix' (Apple Color Emoji)

void ParseSbixStrikes(Span sbix, std::vector<Strike>* strikes) {
  size_t num_strikes = sbix.U32(4);
  for (size_t i = 0; i < num_strikes; ++i) {
    if (!sbix.Has(8 + 4 * i, 4))
      break;
    size_t offset = sbix.U32(8 + 4 * i);
    int ppem = sbix.U16(offset);
    if (ppem > 0)
      strikes->push_back({ppem, ppem, ppem, offset});
  }
}

bool LoadSbixGlyph(const SrcData& src,
                   const Strike& strike,
                   uint32_t glyph_index,
                   GlyphBitmap* out) {
  // 'dupe' records point at another glyph; follow one level only.
  for (int depth = 0; depth < 2; ++depth) {
    if (static_cast<int>(glyph_index) >= src.num_glyphs)
      return false;
    size_t offsets = strike.offset + 4;
    size_t begin = src.sbix.U32(offsets + 4 * size_t{glyph_index});
    size_t end = src.sbix.U32(offsets + 4 * (size_t{glyph_index} + 1));
    if (end <= begin + 8)
      return false;  // Empty glyph.
    Span record = src.sbix.Sub(strike.offset + begin, end - begin);
    if (record.empty())
      return false;

    uint32_t graphic_type = record.U32(4);
    if (graphic_type == kTagDupe) {
      glyph_index = record.U16(8);
      continue;
    }
    if (graphic_type != kTagPng)
      return false;

    out->png = record.Sub(8, record.size - 8);
    out->bearing_x = record.S16(0);
    // originOffsetY places the bitmap's bottom edge; the caller needs the top,
    // which is only known once the PNG is decoded. Store the bottom offset and
    // let the caller add the height.
    out->bearing_y = record.S16(2);

    // sbix reuses the regular 'hmtx' advances, in font units.
    out->advance = 0.0f;
    if (src.units_per_em > 0.0f &&
        FT_Load_Glyph(src.face, glyph_index,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) == 0) {
      out->advance = static_cast<float>(src.face->glyph->metrics.horiAdvance) *
                     strike.ppem_x / src.units_per_em;
    }
    return !out->png.empty();
  }
  return false;
}

//------------------------------------------------------------------------------

// Area-averaging resample of a straight-alpha RGBA image. Averaging happens in
// premultiplied space so transparent pixels do not bleed their color in.
void ResampleRgba(const uint8_t* src,
                  int src_w,
                  int src_h,
                  uint8_t* dst,
                  int dst_w,
                  int dst_h) {
  const float x_ratio = static_cast<float>(src_w) / dst_w;
  const float y_ratio = static_cast<float>(src_h) / dst_h;
  for (int y = 0; y < dst_h; ++y) {
    const float y0 = y * y_ratio;
    const float y1 = y0 + y_ratio;
    const int iy0 = static_cast<int>(y0);
    const int iy1 = std::min(static_cast<int>(std::ceil(y1)), src_h);
    for (int x = 0; x < dst_w; ++x) {
      const float x0 = x * x_ratio;
      const float x1 = x0 + x_ratio;
      const int ix0 = static_cast<int>(x0);
      const int ix1 = std::min(static_cast<int>(std::ceil(x1)), src_w);

      float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      float total = 0.0f;
      for (int sy = iy0; sy < iy1; ++sy) {
        const float wy = std::min(y1, static_cast<float>(sy + 1)) -
                         std::max(y0, static_cast<float>(sy));
        for (int sx = ix0; sx < ix1; ++sx) {
          const float wx = std::min(x1, static_cast<float>(sx + 1)) -
                           std::max(x0, static_cast<float>(sx));
          const float w = wx * wy;
          const uint8_t* p = src + (static_cast<size_t>(sy) * src_w + sx) * 4;
          const float a = p[3] * (1.0f / 255.0f);
          acc[0] += p[0] * a * w;
          acc[1] += p[1] * a * w;
          acc[2] += p[2] * a * w;
          acc[3] += p[3] * w;
          total += w;
        }
      }

      uint8_t* out = dst + (static_cast<size_t>(y) * dst_w + x) * 4;
      if (total <= 0.0f) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }
      const float alpha = acc[3] / total;
      // Back to straight alpha; ImGui's own glyph blitter does the same.
      const float unpremultiply =
          alpha > 0.0f ? 255.0f / (alpha * total) : 0.0f;
      for (int c = 0; c < 3; ++c) {
        out[c] = static_cast<uint8_t>(
            std::min(acc[c] * unpremultiply + 0.5f, 255.0f));
      }
      out[3] = static_cast<uint8_t>(std::min(alpha + 0.5f, 255.0f));
    }
  }
}

//------------------------------------------------------------------------------
// ImFontLoader implementation

bool FontSrcInit(ImFontAtlas*, ImFontConfig* src) {
  IM_ASSERT(src->FontLoaderData == nullptr);
  if (!AcquireFreeType())
    return false;

  auto data = std::make_unique<SrcData>();
  Span font{static_cast<const uint8_t*>(src->FontData),
            static_cast<size_t>(src->FontDataSize)};

  // FontNo packs a variable-font named instance into its high bits (see
  // FT_New_Memory_Face); only the low half indexes a 'ttcf' collection.
  uint32_t face_no = src->FontNo & 0xFFFF;
  bool has_cblc = FindTable(font, face_no, kTagCblc, &data->cblc) &&
                  FindTable(font, face_no, kTagCbdt, &data->cbdt);
  bool has_sbix = FindTable(font, face_no, kTagSbix, &data->sbix);
  if (has_cblc) {
    data->kind = StrikeKind::kCblc;
    ParseCblcStrikes(data->cblc, &data->strikes);
  } else if (has_sbix) {
    data->kind = StrikeKind::kSbix;
    ParseSbixStrikes(data->sbix, &data->strikes);
  }
  if (data->strikes.empty()) {
    ReleaseFreeType();
    return false;
  }

  if (FT_New_Memory_Face(g_ft_library,
                         static_cast<const FT_Byte*>(src->FontData),
                         static_cast<FT_Long>(src->FontDataSize),
                         static_cast<FT_Long>(src->FontNo), &data->face) != 0 ||
      FT_Select_Charmap(data->face, FT_ENCODING_UNICODE) != 0) {
    if (data->face)
      FT_Done_Face(data->face);
    ReleaseFreeType();
    return false;
  }
  data->units_per_em = static_cast<float>(data->face->units_per_EM);
  data->num_glyphs = static_cast<int>(data->face->num_glyphs);

  src->FontLoaderData = data.release();
  return true;
}

void FontSrcDestroy(ImFontAtlas*, ImFontConfig* src) {
  auto* data = static_cast<SrcData*>(src->FontLoaderData);
  if (!data)
    return;
  FT_Done_Face(data->face);
  delete data;
  src->FontLoaderData = nullptr;
  ReleaseFreeType();
}

bool FontSrcContainsGlyph(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
  auto* data = static_cast<SrcData*>(src->FontLoaderData);
  return FT_Get_Char_Index(data->face, codepoint) != 0;
}

bool FontBakedInit(ImFontAtlas*,
                   ImFontConfig* src,
                   ImFontBaked* baked,
                   void* loader_data_for_baked_src) {
  auto* data = static_cast<SrcData*>(src->FontLoaderData);
  auto* baked_data = new (loader_data_for_baked_src) BakedData();

  // Same size derivation as ImGui's FreeType backend.
  float size = baked->Size;
  const float ref_size = baked->OwnerFont->Sources[0]->SizePixels;
  if (src->MergeMode && src->SizePixels != 0.0f && ref_size != 0.0f)
    size *= src->SizePixels / ref_size;
  size *= src->ExtraSizeScale;
  baked_data->density = src->RasterizerDensity * baked->RasterizerDensity;
  const float target = size * baked_data->density;

  // Prefer the smallest strike at least as large as the target, so downscaling
  // (which the resampler handles well) is the common case.
  int best = 0;
  for (int i = 1; i < static_cast<int>(data->strikes.size()); ++i) {
    const float candidate = static_cast<float>(data->strikes[i].line_height);
    const float current = static_cast<float>(data->strikes[best].line_height);
    const bool candidate_fits = candidate >= target;
    const bool current_fits = current >= target;
    if (candidate_fits != current_fits
            ? candidate_fits
            : std::abs(candidate - target) < std::abs(current - target))
      best = i;
  }
  baked_data->strike_index = best;
  baked_data->scale = target / data->strikes[best].line_height;
  return true;
}

void FontBakedDestroy(ImFontAtlas*,
                      ImFontConfig*,
                      ImFontBaked*,
                      void* loader_data_for_baked_src) {
  static_cast<BakedData*>(loader_data_for_baked_src)->~BakedData();
}

bool FontBakedLoadGlyph(ImFontAtlas* atlas,
                        ImFontConfig* src,
                        ImFontBaked* baked,
                        void* loader_data_for_baked_src,
                        ImWchar codepoint,
                        ImFontGlyph* out_glyph,
                        float* out_advance_x) {
  auto* data = static_cast<SrcData*>(src->FontLoaderData);
  auto* baked_data = static_cast<BakedData*>(loader_data_for_baked_src);
  const Strike& strike = data->strikes[baked_data->strike_index];

  uint32_t glyph_index = FT_Get_Char_Index(data->face, codepoint);
  if (glyph_index == 0)
    return false;

  GlyphBitmap bitmap;
  const bool found = data->kind == StrikeKind::kCblc
                         ? LoadCbdtGlyph(*data, strike, glyph_index, &bitmap)
                         : LoadSbixGlyph(*data, strike, glyph_index, &bitmap);
  if (!found)
    return false;

  const float scale = baked_data->scale;
  const float recip_density = 1.0f / baked_data->density;
  const float advance_x = bitmap.advance * scale * recip_density;
  if (out_advance_x) {
    IM_ASSERT(out_glyph == nullptr);
    *out_advance_x = advance_x;
    return true;
  }

  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(
      bitmap.png.data, static_cast<int>(bitmap.png.size), &w, &h, &channels, 4);
  if (!pixels)
    return false;

  // sbix positions the bitmap by its bottom edge; convert to a top bearing.
  if (data->kind == StrikeKind::kSbix)
    bitmap.bearing_y += h;

  out_glyph->Codepoint = codepoint;
  out_glyph->AdvanceX = advance_x;

  const int dst_w = std::max(1, static_cast<int>(std::lround(w * scale)));
  const int dst_h = std::max(1, static_cast<int>(std::lround(h * scale)));
  ImFontAtlasRectId pack_id = ImFontAtlasPackAddRect(atlas, dst_w, dst_h);
  if (pack_id == ImFontAtlasRectId_Invalid) {
    IM_ASSERT(pack_id != ImFontAtlasRectId_Invalid && "Out of texture memory.");
    stbi_image_free(pixels);
    return false;
  }
  ImTextureRect* rect = ImFontAtlasPackGetRect(atlas, pack_id);

  atlas->Builder->TempBuffer.resize(static_cast<size_t>(dst_w) * dst_h * 4);
  uint8_t* temp_buffer = atlas->Builder->TempBuffer.Data;
  ResampleRgba(pixels, w, h, temp_buffer, dst_w, dst_h);
  stbi_image_free(pixels);

  const float ref_size = baked->OwnerFont->Sources[0]->SizePixels;
  const float offsets_scale = ref_size != 0.0f ? baked->Size / ref_size : 1.0f;
  const float font_off_x = ImFloor(src->GlyphOffset.x * offsets_scale + 0.5f);
  const float font_off_y =
      ImFloor(src->GlyphOffset.y * offsets_scale + 0.5f) + baked->Ascent;

  out_glyph->X0 = bitmap.bearing_x * scale * recip_density + font_off_x;
  out_glyph->Y0 = -bitmap.bearing_y * scale * recip_density + font_off_y;
  out_glyph->X1 = out_glyph->X0 + dst_w * recip_density;
  out_glyph->Y1 = out_glyph->Y0 + dst_h * recip_density;
  out_glyph->Visible = true;
  out_glyph->Colored = true;
  out_glyph->PackId = pack_id;
  ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out_glyph, rect,
                                     temp_buffer, ImTextureFormat_RGBA32,
                                     dst_w * 4);
  return true;
}

}  // namespace

bool FontFileHasColorBitmapStrikes(const std::string& path, int face_index) {
  base::ScopedFILE fp(fopen(path.c_str(), "rb"));
  if (!fp)
    return false;
  // The sfnt table directory sits at the head of the file; a 'ttcf' collection
  // adds its own offset table, so read enough to cover a few nested
  // directories rather than mapping the whole (often multi-megabyte) font.
  std::vector<uint8_t> header(size_t{64} * 1024);
  header.resize(fread(header.data(), 1, header.size(), fp.get()));
  Span font{header.data(), header.size()};

  // Only the requested face matters: a collection can mix faces that carry
  // strikes with faces that do not, and answering for the wrong one would
  // select a loader that then finds nothing to rasterize.
  uint32_t face_no = static_cast<uint32_t>(face_index) & 0xFFFF;
  size_t offset = 0;
  size_t size = 0;
  return (FindTableRange(font, face_no, kTagCblc, &offset, &size) &&
          FindTableRange(font, face_no, kTagCbdt, &offset, &size)) ||
         FindTableRange(font, face_no, kTagSbix, &offset, &size);
}

const ImFontLoader* GetColorBitmapFontLoader() {
  static ImFontLoader loader = [] {
    ImFontLoader l;
    l.Name = "ColorBitmap";
    l.FontSrcInit = FontSrcInit;
    l.FontSrcDestroy = FontSrcDestroy;
    l.FontSrcContainsGlyph = FontSrcContainsGlyph;
    l.FontBakedInit = FontBakedInit;
    l.FontBakedDestroy = FontBakedDestroy;
    l.FontBakedLoadGlyph = FontBakedLoadGlyph;
    l.FontBakedSrcLoaderDataSize = sizeof(BakedData);
    return l;
  }();
  return &loader;
}

}  // namespace eng
