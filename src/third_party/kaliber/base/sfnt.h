#ifndef BASE_SFNT_H
#define BASE_SFNT_H

#include <cstddef>
#include <cstdint>
#include <string>

// Minimal read-only sfnt (TrueType/OpenType) parsing, shared by the font
// loader and the platform font enumerators. Only the table directory and the
// 'name' table are understood; glyph data is left to FreeType.
namespace base::sfnt {

constexpr uint32_t kTagTtcf = 0x74746366;  // 'ttcf'
constexpr uint32_t kTagName = 0x6E616D65;  // 'name'

// Bounds-checked big-endian view over a slice of the font file. Reads past the
// end return zero and Sub() clamps, so a truncated or hostile font yields empty
// results instead of out-of-bounds access.
struct Span {
  const uint8_t* data = nullptr;
  size_t size = 0;

  bool empty() const { return data == nullptr || size == 0; }

  bool Has(size_t offset, size_t length) const {
    return data && offset <= size && length <= size - offset;
  }

  uint8_t U8(size_t offset) const { return Has(offset, 1) ? data[offset] : 0; }
  int8_t S8(size_t offset) const { return static_cast<int8_t>(U8(offset)); }

  uint16_t U16(size_t offset) const {
    if (!Has(offset, 2))
      return 0;
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
  }
  int16_t S16(size_t offset) const { return static_cast<int16_t>(U16(offset)); }

  uint32_t U32(size_t offset) const {
    if (!Has(offset, 4))
      return 0;
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
  }

  Span Sub(size_t offset, size_t length) const {
    if (!Has(offset, length))
      return {};
    return {data + offset, length};
  }
};

// Looks up a table in an sfnt font directory, reading only the directory
// itself. Handles 'ttcf' collections via font_no.
bool FindTableRange(Span font,
                    uint32_t font_no,
                    uint32_t tag,
                    size_t* out_offset,
                    size_t* out_size);

// As above, but also slices out the table contents.
bool FindTable(Span font, uint32_t font_no, uint32_t tag, Span* out);

// Number of faces in the file; 1 for anything that is not a 'ttcf' collection.
uint32_t CountFaces(Span font);

// Returns the index of the face in `path` whose PostScript name matches
// `ps_name`, or 0 when the file is not a collection, cannot be read, or holds
// no matching face. Used on platforms whose font APIs expose a file but not
// which face inside it was matched.
int FindFaceIndexByPostScriptName(const std::string& path,
                                  const std::string& ps_name);

}  // namespace base::sfnt

#endif  // BASE_SFNT_H
