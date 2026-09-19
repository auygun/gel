#include "third_party/kaliber/base/sfnt.h"

#include <vector>

#include "third_party/kaliber/base/file.h"

namespace base::sfnt {

namespace {

// Name ID 6 of the OpenType 'name' table.
constexpr uint16_t kNameIdPostScript = 6;

// Table directories sit at the head of the file; a 'ttcf' collection adds its
// own offset table in front of them. Reading this much covers the collection
// header plus the per-face directories without mapping a font that is often
// tens of megabytes.
constexpr size_t kHeaderReadSize = 64 * 1024;

void AppendUtf8(uint32_t cp, std::string* out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string DecodeUtf16BE(Span s) {
  std::string out;
  for (size_t i = 0; i + 1 < s.size; i += 2) {
    uint32_t cp = s.U16(i);
    // Combine a surrogate pair into the codepoint it encodes.
    if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < s.size) {
      uint32_t lo = s.U16(i + 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    AppendUtf8(cp, &out);
  }
  return out;
}

// Mac Roman and Latin-1 agree over ASCII, which is all a PostScript name may
// contain, so the high range is not worth a translation table here.
std::string DecodeMacRoman(Span s) {
  std::string out;
  for (size_t i = 0; i < s.size; ++i)
    AppendUtf8(s.U8(i), &out);
  return out;
}

std::string GetNameFromTable(Span name_table, uint16_t name_id) {
  uint16_t count = name_table.U16(2);
  size_t storage = name_table.U16(4);
  std::string mac_fallback;
  for (uint16_t i = 0; i < count; ++i) {
    size_t rec = 6 + size_t{i} * 12;
    if (!name_table.Has(rec, 12))
      break;
    if (name_table.U16(rec + 6) != name_id)
      continue;
    uint16_t platform = name_table.U16(rec);
    uint16_t encoding = name_table.U16(rec + 2);
    uint16_t language = name_table.U16(rec + 4);
    Span str = name_table.Sub(storage + name_table.U16(rec + 10),
                              name_table.U16(rec + 8));
    if (str.empty())
      continue;
    // Windows records are UTF-16BE; take en-US in preference to any other
    // language so a localized name does not win.
    if (platform == 3 && (encoding == 1 || encoding == 10)) {
      if (language == 0x409)
        return DecodeUtf16BE(str);
      if (mac_fallback.empty())
        mac_fallback = DecodeUtf16BE(str);
    } else if (platform == 1 && encoding == 0 && mac_fallback.empty()) {
      mac_fallback = DecodeMacRoman(str);
    }
  }
  return mac_fallback;
}

}  // namespace

bool FindTableRange(Span font,
                    uint32_t font_no,
                    uint32_t tag,
                    size_t* out_offset,
                    size_t* out_size) {
  size_t base = 0;
  if (font.U32(0) == kTagTtcf) {
    uint32_t num_fonts = font.U32(8);
    if (font_no >= num_fonts)
      return false;
    base = font.U32(12 + 4 * size_t{font_no});
  }
  size_t num_tables = font.U16(base + 4);
  for (size_t i = 0; i < num_tables; ++i) {
    size_t entry = base + 12 + 16 * i;
    if (!font.Has(entry, 16))
      return false;
    if (font.U32(entry) != tag)
      continue;
    *out_offset = font.U32(entry + 8);
    *out_size = font.U32(entry + 12);
    return true;
  }
  return false;
}

bool FindTable(Span font, uint32_t font_no, uint32_t tag, Span* out) {
  size_t offset = 0;
  size_t size = 0;
  if (!FindTableRange(font, font_no, tag, &offset, &size))
    return false;
  Span table = font.Sub(offset, size);
  if (table.empty())
    return false;
  *out = table;
  return true;
}

uint32_t CountFaces(Span font) {
  return font.U32(0) == kTagTtcf ? font.U32(8) : 1;
}

int FindFaceIndexByPostScriptName(const std::string& path,
                                  const std::string& ps_name) {
  if (ps_name.empty())
    return 0;
  ScopedFILE fp(fopen(path.c_str(), "rb"));
  if (!fp)
    return 0;

  std::vector<uint8_t> header(kHeaderReadSize);
  header.resize(fread(header.data(), 1, header.size(), fp.get()));
  Span font{header.data(), header.size()};
  if (font.U32(0) != kTagTtcf)
    return 0;

  uint32_t num_faces = CountFaces(font);
  for (uint32_t i = 0; i < num_faces; ++i) {
    // The 'name' table itself usually sits past the header read, so pull just
    // that table rather than the whole font.
    size_t offset = 0;
    size_t size = 0;
    if (!FindTableRange(font, i, kTagName, &offset, &size) || size == 0)
      continue;
    std::vector<uint8_t> table(size);
    if (fseek(fp.get(), static_cast<long>(offset), SEEK_SET) != 0)
      continue;
    table.resize(fread(table.data(), 1, table.size(), fp.get()));
    if (GetNameFromTable(Span{table.data(), table.size()}, kNameIdPostScript) ==
        ps_name)
      return static_cast<int>(i);
  }
  return 0;
}

}  // namespace base::sfnt
