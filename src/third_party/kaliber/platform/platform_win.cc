#include "third_party/kaliber/platform/platform.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>

#include <dwmapi.h>
#include <dwrite_1.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#include <wrl/client.h>

#include "third_party/glfw/glfw/include/GLFW/glfw3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "third_party/glfw/glfw/include/GLFW/glfw3native.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace eng {

namespace {

// Windows exposes the "Make text bigger" accessibility slider separately from
// display scaling, as a percentage between 100 and 225.  Chromium reads it
// through the UWP UISettings object (ui/display/win/uwp_text_scale_factor.cc);
// this registry value is what backs that setting, and reading it directly
// avoids pulling WinRT in for a single number.  The Linux backend folds the
// equivalent gtk-xft-dpi text scale into the device scale the same way.
float GetTextScaleFactor() {
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Accessibility", 0,
                    KEY_READ, &key) != ERROR_SUCCESS) {
    return 1.0f;
  }
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = REG_NONE;
  float scale = 1.0f;
  if (RegQueryValueExA(key, "TextScaleFactor", nullptr, &type,
                       reinterpret_cast<BYTE*>(&value),
                       &size) == ERROR_SUCCESS &&
      type == REG_DWORD && value >= 100u) {
    scale = static_cast<float>(value) / 100.0f;
  }
  RegCloseKey(key);
  return scale;
}

}  // namespace

std::filesystem::path Platform::GetSettingsPath() {
  PWSTR wide_path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                     &wide_path))) {
    std::filesystem::path result =
        std::filesystem::path(wide_path) / "gel" / "settings.json";
    CoTaskMemFree(wide_path);
    return result;
  }
  return {};
}

void Platform::PlatformInit() {
  HWND hwnd = glfwGetWin32Window(window_);
  if (IsDarkMode()) {
    BOOL use_dark_mode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_mode,
                          sizeof(use_dark_mode));
  }
  if (!decorated_) {
    // Let Window handle resize natively via WS_THICKFRAME, so no custom
    // interactive resize is needed.
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    // After changing the style, we need to notify the window manager.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

void Platform::PlatformShutdown() {}

bool Platform::PlatformUpdate(double /*wait_timeout*/) {
  return false;
}

float Platform::PlatformDetectScaleFactor() {
  // Use the Win32 DPI API directly instead of glfwGetWindowContentScale.
  // GLFW internally does the same thing, but loading GetDpiForWindow
  // dynamically here avoids a hard SDK dependency on Windows 10 1607+, and
  // the explicit GetDeviceCaps fallback makes the behavior clear.
  constexpr float kDefaultDpi = 96.0f;
  HWND hwnd = glfwGetWin32Window(window_);

  // The accessibility text scale is independent of the monitor DPI and applies
  // on top of it.
  const float text_scale = GetTextScaleFactor();

  // GetDpiForWindow is available on Windows 10 1607+ (build 14393).
  // Returns per-monitor DPI for the monitor the window is on.
  // Load dynamically to avoid a hard dependency on that SDK version.
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  HMODULE user32 = GetModuleHandleA("user32.dll");
  if (user32) {
    auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (fn) {
      UINT dpi = fn(hwnd);
      if (dpi > 0)
        return dpi / kDefaultDpi * text_scale;
    }
  }

  // Fallback: system-wide DPI from the desktop DC.
  HDC hdc = GetDC(hwnd);
  int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(hwnd, hdc);
  return (dpi > 0 ? dpi / kDefaultDpi : 1.0f) * text_scale;
}

void Platform::SetWindowBackgroundColor(float, float, float) {}

bool Platform::IsDarkMode() const {
  HKEY key;
  if (RegOpenKeyExA(
          HKEY_CURRENT_USER,
          "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          0, KEY_READ, &key) == ERROR_SUCCESS) {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegQueryValueExA(key, "AppsUseLightTheme", nullptr, nullptr,
                     reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return value == 0;
  }
  return true;
}

namespace {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

std::string ToUtf8(const wchar_t* str, UINT32 len) {
  if (!str || len == 0)
    return {};
  int bytes = WideCharToMultiByte(CP_UTF8, 0, str, static_cast<int>(len),
                                  nullptr, 0, nullptr, nullptr);
  if (bytes <= 0)
    return {};
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, str, static_cast<int>(len), out.data(), bytes,
                      nullptr, nullptr);
  return out;
}

// Picks the en-us entry from a DirectWrite localized string, falling back to
// the first one. GDI returned names in the active code page, which mangled
// every non-Latin family name; these are UTF-16 and convert cleanly.
std::string GetLocalizedString(IDWriteLocalizedStrings* strings) {
  if (!strings || strings->GetCount() == 0)
    return {};
  UINT32 index = 0;
  BOOL exists = FALSE;
  if (FAILED(strings->FindLocaleName(L"en-us", &index, &exists)) || !exists)
    index = 0;
  UINT32 len = 0;
  if (FAILED(strings->GetStringLength(index, &len)))
    return {};
  std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
  if (FAILED(strings->GetString(index, buf.data(),
                                static_cast<UINT32>(buf.size()))))
    return {};
  return ToUtf8(buf.c_str(), len);
}

// Resolves the on-disk path of a font face. Fonts served by a custom loader
// (memory-resident or remote) have no path and are skipped.
std::string GetFontFacePath(IDWriteFontFace* face) {
  UINT32 file_count = 1;
  ComPtr<IDWriteFontFile> file;
  if (FAILED(face->GetFiles(&file_count, file.GetAddressOf())) ||
      file_count != 1 || !file) {
    return {};
  }
  const void* key = nullptr;
  UINT32 key_size = 0;
  ComPtr<IDWriteFontFileLoader> loader;
  if (FAILED(file->GetReferenceKey(&key, &key_size)) ||
      FAILED(file->GetLoader(&loader))) {
    return {};
  }
  ComPtr<IDWriteLocalFontFileLoader> local;
  if (FAILED(loader.As(&local)))
    return {};
  UINT32 len = 0;
  if (FAILED(local->GetFilePathLengthFromKey(key, key_size, &len)))
    return {};
  std::wstring path(static_cast<size_t>(len) + 1, L'\0');
  if (FAILED(local->GetFilePathFromKey(key, key_size, path.data(),
                                       static_cast<UINT32>(path.size())))) {
    return {};
  }
  return ToUtf8(path.c_str(), len);
}

// FreeType sizes faces with FT_Request_Size(), which fails on bitmap faces,
// so only outline formats are offered.
bool IsScalableFormat(IDWriteFontFace* face) {
  switch (face->GetType()) {
    case DWRITE_FONT_FACE_TYPE_CFF:
    case DWRITE_FONT_FACE_TYPE_TRUETYPE:
    case DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION:
      return true;
    default:
      return false;
  }
}

bool IsMonospaced(IDWriteFont* font, IDWriteFontFace* face) {
  // IsMonospacedFont() is the direct answer but needs Windows 8.
  ComPtr<IDWriteFont1> font1;
  if (SUCCEEDED(ComPtr<IDWriteFont>(font).As(&font1)) && font1)
    return font1->IsMonospacedFont() != FALSE;

  // Otherwise compare the advances of a narrow and a wide glyph.
  const UINT32 codepoints[] = {U'i', U'W'};
  UINT16 glyphs[2] = {};
  if (FAILED(face->GetGlyphIndices(codepoints, 2, glyphs)) || !glyphs[0] ||
      !glyphs[1]) {
    return false;
  }
  DWRITE_GLYPH_METRICS metrics[2] = {};
  if (FAILED(face->GetDesignGlyphMetrics(glyphs, 2, metrics, FALSE)))
    return false;
  return metrics[0].advanceWidth == metrics[1].advanceWidth;
}

}  // namespace

std::vector<Platform::FontInfo> Platform::GetFonts() const {
  std::vector<FontInfo> result;
  // DirectWrite replaces the old GDI enumeration plus registry path lookup:
  // EnumFontFamiliesEx() ignores lfPitchAndFamily (documented as "must be set
  // to zero"), so that path never filtered on pitch at all, and mapping a face
  // name back to a file through the registry mismatched families whose names
  // share a prefix.
  ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                 __uuidof(IDWriteFactory),
                                 (IUnknown**)factory.GetAddressOf()))) {
    return result;
  }
  ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection, FALSE)))
    return result;

  UINT32 family_count = collection->GetFontFamilyCount();
  for (UINT32 i = 0; i < family_count; i++) {
    ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(i, &family)))
      continue;

    // Ask for the regular face directly rather than guessing from names.
    ComPtr<IDWriteFont> font;
    if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, &font))) {
      continue;
    }
    if (font->IsSymbolFont())
      continue;
    // A font that cannot draw plain ASCII is useless as a UI font.
    BOOL has_latin = FALSE;
    if (FAILED(font->HasCharacter(L'A', &has_latin)) || !has_latin)
      continue;

    ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face)) || !IsScalableFormat(face.Get()))
      continue;

    ComPtr<IDWriteLocalizedStrings> family_names;
    if (FAILED(family->GetFamilyNames(&family_names)))
      continue;
    std::string name = GetLocalizedString(family_names.Get());
    std::string path = GetFontFacePath(face.Get());
    if (name.empty() || path.empty())
      continue;

    result.push_back({name, path, static_cast<int>(face->GetIndex()),
                      IsMonospaced(font.Get(), face.Get())});
  }
  // DirectWrite enumerates families in collection order; the other backends
  // come out of a std::map, so sort here to match.
  std::sort(
      result.begin(), result.end(),
      [](const FontInfo& a, const FontInfo& b) { return a.name < b.name; });
  return result;
}

std::vector<Platform::FontInfo> Platform::GetFallbackFonts() const {
  std::vector<FontInfo> result;
  char fonts_dir[MAX_PATH];
  if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, 0, fonts_dir)))
    return result;
  // Adds the first candidate that exists. Face 0 of a collection is the base
  // face; the others are UI and proportional variants.
  auto add_first_present = [&](std::span<const char* const> candidates) {
    for (const char* name : candidates) {
      std::string path = std::string(fonts_dir) + "\\" + name;
      if (std::filesystem::exists(path)) {
        result.push_back({std::string(), path, 0});
        return;
      }
    }
  };

  // Han and Hangul need a font each. None of the Han fonts Windows ships
  // covers Hangul, so picking a single CJK font left Korean as the placeholder
  // glyph: Microsoft YaHei is present on every install and has Han and kana
  // but no Hangul, so Malgun Gothic was never reached.
  const char* han_candidates[] = {"msyh.ttc", "msgothic.ttc", "simsun.ttc"};
  const char* hangul_candidates[] = {"malgun.ttf", "gulim.ttc", "batang.ttc"};
  add_first_present(han_candidates);
  add_first_present(hangul_candidates);

  // A symbol font for dingbats such as ❌ (U+274C) that the default font may
  // not cover. Segoe UI Symbol covers pictographs monochromatically.
  // Segoe UI Emoji (seguiemj.ttf) is deliberately not used: on Windows 11 it
  // is a COLRv1 font, which FreeType only exposes through the paint-graph API
  // (TT_SUPPORT_COLRV1) and never rasterizes, so merging it ahead of
  // seguisym.ttf would replace monochrome glyphs with blanks.
  const char* symbol_candidates[] = {"seguisym.ttf"};
  add_first_present(symbol_candidates);
  // Windows has no fontconfig-style "font covering this codepoint" query, so
  // the per-script fonts of kFallbackFontProbes are named directly. Missing
  // files are skipped: which of these ship depends on the Windows edition and
  // the installed language packs. See kFallbackFontProbes for the caveat that
  // coverage is not the same as correct rendering.
  const char* script_candidates[] = {
      "arial.ttf",     // Arabic, Hebrew
      "sylfaen.ttf",   // Armenian, Georgian
      "estre.ttf",     // Syriac (Estrangelo Edessa)
      "mvboli.ttf",    // Thaana (MV Boli)
      "Nirmala.ttf",   // Devanagari, Bengali, Tamil and the other Indic blocks
      "leelawui.ttf",  // Thai, Lao (Leelawadee UI)
      "himalaya.ttf",  // Tibetan
      "mmrtext.ttf",   // Myanmar
      "nyala.ttf",     // Ethiopic
      "gadugi.ttf",    // Cherokee, Unified Canadian Aboriginal Syllabics
      "khmerui.ttf",   // Khmer
      "monbaiti.ttf",  // Mongolian
      "ebrima.ttf",    // N'Ko, Tifinagh, Vai and other African scripts
  };
  for (const char* name : script_candidates) {
    std::string path = std::string(fonts_dir) + "\\" + name;
    if (std::filesystem::exists(path))
      result.push_back({std::string(), path, 0});
  }
  return result;
}

void Platform::OpenURL(const char* url) {
  ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void Platform::SetPrimarySelection(const char* /*text*/) {}
const char* Platform::GetPrimarySelection() {
  return "";
}

void Platform::BeginInteractiveMove() {
  HWND hwnd = glfwGetWin32Window(window_);
  ReleaseCapture();
  SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
  mouse_buttons_down_[static_cast<int>(MouseButton::Left)] = false;
}

void Platform::ShowWindowMenu(int x, int y) {
  HWND hwnd = glfwGetWin32Window(window_);
  HMENU menu = GetSystemMenu(hwnd, FALSE);
  if (!menu)
    return;
  POINT pt = {x, y};
  ClientToScreen(hwnd, &pt);
  int cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                           pt.x, pt.y, 0, hwnd, nullptr);
  if (cmd)
    PostMessage(hwnd, WM_SYSCOMMAND, cmd, 0);
}

Platform::CSDButtonVisibility Platform::GetCSDButtonVisibility() const {
  return {};
}

void Platform::BeginInteractiveResize(int edges) {
  // Windows handles resize natively via WS_THICKFRAME even for borderless
  // windows, so no custom interactive resize is needed.
}

}  // namespace eng
