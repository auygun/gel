#ifndef ENGINE_COLOR_BITMAP_FONT_LOADER_H
#define ENGINE_COLOR_BITMAP_FONT_LOADER_H

#include <string>

struct ImFontLoader;

namespace eng {

// Returns true if the font file carries color bitmap strikes ('CBLC'/'CBDT' as
// used by Noto Color Emoji, or 'sbix' as used by Apple Color Emoji). ImGui's
// FreeType loader cannot rasterize those: it sizes faces with
// FT_Request_Size(), which fails on non-scalable faces, and the vendored
// FreeType is built without FT_CONFIG_OPTION_USE_PNG so it cannot decode the
// PNG payloads either. Only the sfnt table directory is read.
//
// face_index selects the face within a 'ttcf' collection, in the same packing
// ImFontConfig::FontNo uses (high bits are a variable-font named instance and
// are ignored here).
bool FontFileHasColorBitmapStrikes(const std::string& path, int face_index);

// A per-source ImFontLoader (ImFontConfig::FontLoader) that rasterizes those
// strikes: it picks the strike closest to the baked size, decodes the PNG with
// stb_image and box-filters it down to the text size.
//
// LoaderInit/LoaderShutdown are intentionally left null; ImGui asserts that
// per-source loaders do not use them (imgui_draw.cpp, ImFontAtlasBuildAddFont).
// The shared FT_Library is refcounted across FontSrcInit/FontSrcDestroy
// instead.
const ImFontLoader* GetColorBitmapFontLoader();

}  // namespace eng

#endif  // ENGINE_COLOR_BITMAP_FONT_LOADER_H
