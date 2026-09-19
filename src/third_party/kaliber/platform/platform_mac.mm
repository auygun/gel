#include "third_party/kaliber/platform/platform.h"

#import <AppKit/NSApplication.h>
#import <AppKit/NSImage.h>
#import <AppKit/NSWorkspace.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#import <Foundation/NSPathUtilities.h>
#include <limits.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#include "third_party/kaliber/base/sfnt.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include "third_party/glfw/glfw/include/GLFW/glfw3native.h"

namespace eng {

std::filesystem::path Platform::GetSettingsPath() {
  @autoreleasepool {
    NSArray* paths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
      return std::filesystem::path([paths[0] fileSystemRepresentation]) /
             "gel" / "settings.json";
    }
  }
  return {};
}

void Platform::PlatformInit() {}

void Platform::PlatformShutdown() {}

bool Platform::PlatformUpdate(double /*wait_timeout*/) {
  return false;
}

float Platform::PlatformDetectScaleFactor() {
  // GLFW's GLFW_SCALE_FRAMEBUFFER (default true) already scales the framebuffer
  // by backingScaleFactor on macOS, so ImGui picks up the Retina display scale
  // via DisplayFramebufferScale (= framebuffer_size / window_size).  Returning
  // the backing scale factor here would double-apply DPI scaling to fonts and
  // style sizes.
  return 1.0f;
}

void Platform::SetWindowBackgroundColor(float, float, float) {}

bool Platform::IsDarkMode() const {
  return true;
}

namespace {

// Copies a CFString out as UTF-8. Returns an empty string if it does not fit.
std::string ToUtf8(CFStringRef str) {
  if (!str)
    return {};
  CFIndex len = CFStringGetLength(str);
  CFIndex capacity =
      CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
  std::string out(static_cast<size_t>(capacity), '\0');
  if (!CFStringGetCString(str, out.data(), capacity, kCFStringEncodingUTF8))
    return {};
  out.resize(strlen(out.c_str()));
  return out;
}

std::string ToPath(CFURLRef url) {
  if (!url)
    return {};
  char buf[PATH_MAX];
  if (!CFURLGetFileSystemRepresentation(url, true, (UInt8*)buf, sizeof(buf)))
    return {};
  return buf;
}

// Bitmap suitcases (.dfont and friends) and anything CoreText cannot classify
// are rejected: FreeType sizes faces with FT_Request_Size(), which fails on
// non-scalable faces.
bool IsScalableFormat(CTFontDescriptorRef fd) {
  CFNumberRef format_num =
      (CFNumberRef)CTFontDescriptorCopyAttribute(fd, kCTFontFormatAttribute);
  if (!format_num)
    return false;
  int32_t format = kCTFontFormatUnrecognized;
  CFNumberGetValue(format_num, kCFNumberSInt32Type, &format);
  CFRelease(format_num);
  return format == kCTFontFormatOpenTypeTrueType ||
         format == kCTFontFormatOpenTypePostScript ||
         format == kCTFontFormatTrueType || format == kCTFontFormatPostScript;
}

// A font that cannot draw plain ASCII is useless as a UI font, whatever its
// traits say. This is what keeps emoji, dingbat and icon fonts out of the
// list -- Apple Color Emoji, for one, carries the monospace trait.
bool CoversBasicLatin(CTFontDescriptorRef fd) {
  CFCharacterSetRef cs = (CFCharacterSetRef)CTFontDescriptorCopyAttribute(
      fd, kCTFontCharacterSetAttribute);
  if (!cs)
    return false;
  bool covers = CFCharacterSetIsLongCharacterMember(cs, 'A') &&
                CFCharacterSetIsLongCharacterMember(cs, 'z') &&
                CFCharacterSetIsLongCharacterMember(cs, '0');
  CFRelease(cs);
  return covers;
}

// Reads the slant, weight and spacing traits. Returns false for italic and
// oblique faces, which are never offered as the UI font.
bool GetUprightTraits(CTFontDescriptorRef fd,
                      float* out_weight,
                      bool* out_monospace) {
  CFDictionaryRef traits = (CFDictionaryRef)CTFontDescriptorCopyAttribute(
      fd, kCTFontTraitsAttribute);
  if (!traits)
    return false;
  bool upright = true;
  float slant = 0;
  CFNumberRef slant_num =
      (CFNumberRef)CFDictionaryGetValue(traits, kCTFontSlantTrait);
  if (slant_num) {
    CFNumberGetValue(slant_num, kCFNumberFloatType, &slant);
    upright = slant <= 0.01f && slant >= -0.01f;
  }
  // kCTFontWeightTrait is normalized to [-1, 1] with 0 at regular.
  *out_weight = 0;
  CFNumberRef weight_num =
      (CFNumberRef)CFDictionaryGetValue(traits, kCTFontWeightTrait);
  if (weight_num)
    CFNumberGetValue(weight_num, kCFNumberFloatType, out_weight);
  // The symbolic traits carry the spacing bit; a font is reported as fixed
  // pitch only when it sets kCTFontTraitMonoSpace.
  *out_monospace = false;
  CFNumberRef symbolic_num =
      (CFNumberRef)CFDictionaryGetValue(traits, kCTFontSymbolicTrait);
  if (symbolic_num) {
    int32_t symbolic = 0;
    CFNumberGetValue(symbolic_num, kCFNumberSInt32Type, &symbolic);
    *out_monospace = (symbolic & kCTFontTraitMonoSpace) != 0;
  }
  CFRelease(traits);
  return upright;
}

}  // namespace

std::vector<Platform::FontInfo> Platform::GetFonts() const {
  std::vector<FontInfo> result;
  CTFontCollectionRef collection =
      CTFontCollectionCreateFromAvailableFonts(nullptr);
  if (!collection)
    return result;
  CFArrayRef matches =
      CTFontCollectionCreateMatchingFontDescriptors(collection);
  CFRelease(collection);
  if (matches) {
    // Collect one entry per family name, preferring weight closest to regular.
    struct Entry {
      std::string path;
      std::string ps_name;
      float weight;
      bool is_monospace;
    };
    std::map<std::string, Entry> families;
    CFIndex count = CFArrayGetCount(matches);
    for (CFIndex i = 0; i < count; i++) {
      CTFontDescriptorRef fd =
          (CTFontDescriptorRef)CFArrayGetValueAtIndex(matches, i);

      float weight = 0;
      bool is_monospace = false;
      if (!GetUprightTraits(fd, &weight, &is_monospace) ||
          !IsScalableFormat(fd) || !CoversBasicLatin(fd))
        continue;

      CFStringRef family_name = (CFStringRef)CTFontDescriptorCopyAttribute(
          fd, kCTFontFamilyNameAttribute);
      CFStringRef ps_name =
          (CFStringRef)CTFontDescriptorCopyAttribute(fd, kCTFontNameAttribute);
      CFURLRef url =
          (CFURLRef)CTFontDescriptorCopyAttribute(fd, kCTFontURLAttribute);
      std::string name = ToUtf8(family_name);
      std::string path = ToPath(url);
      std::string ps = ToUtf8(ps_name);
      if (family_name)
        CFRelease(family_name);
      if (ps_name)
        CFRelease(ps_name);
      if (url)
        CFRelease(url);
      if (name.empty() || path.empty())
        continue;

      auto it = families.find(name);
      if (it == families.end() ||
          std::abs(weight) < std::abs(it->second.weight)) {
        families[name] = {std::move(path), std::move(ps), weight, is_monospace};
      }
    }
    for (auto& [name, entry] : families) {
      // CoreText exposes the file but not which face inside it matched, so the
      // index has to come from the file itself.
      int face =
          base::sfnt::FindFaceIndexByPostScriptName(entry.path, entry.ps_name);
      result.push_back({name, entry.path, face, entry.is_monospace});
    }
    CFRelease(matches);
  }
  return result;
}

// Resolves the system font face that covers the given codepoint.
static Platform::FontInfo ResolveFontForCodepoint(unsigned int codepoint) {
  // CTFontCreateForString() works on UTF-16, so the range must span the whole
  // string: a non-BMP codepoint is a surrogate pair and asking about just its
  // first unit resolves nothing.
  char utf8[5] = {};
  int len = 0;
  if (codepoint < 0x80) {
    utf8[len++] = static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    utf8[len++] = static_cast<char>(0xC0 | (codepoint >> 6));
    utf8[len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    utf8[len++] = static_cast<char>(0xE0 | (codepoint >> 12));
    utf8[len++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    utf8[len++] = static_cast<char>(0xF0 | (codepoint >> 18));
    utf8[len++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    utf8[len++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  }

  Platform::FontInfo result;
  CFStringRef test_str = CFStringCreateWithCString(kCFAllocatorDefault, utf8,
                                                   kCFStringEncodingUTF8);
  CTFontRef font = CTFontCreateWithName(CFSTR(""), 12.0, nullptr);
  if (font && test_str) {
    CTFontRef resolved = CTFontCreateForString(
        font, test_str, CFRangeMake(0, CFStringGetLength(test_str)));
    if (resolved) {
      CTFontDescriptorRef desc = CTFontCopyFontDescriptor(resolved);
      if (desc) {
        CFURLRef url =
            (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
        CFStringRef ps_name = (CFStringRef)CTFontDescriptorCopyAttribute(
            desc, kCTFontNameAttribute);
        result.path = ToPath(url);
        // Most macOS system fonts are .ttc collections, so without the index
        // every fallback would merge face 0 whatever the probe resolved to.
        result.face_index = base::sfnt::FindFaceIndexByPostScriptName(
            result.path, ToUtf8(ps_name));
        if (ps_name)
          CFRelease(ps_name);
        if (url)
          CFRelease(url);
        CFRelease(desc);
      }
      CFRelease(resolved);
    }
  }
  if (font)
    CFRelease(font);
  if (test_str)
    CFRelease(test_str);
  return result;
}

std::vector<Platform::FontInfo> Platform::GetFallbackFonts() const {
  std::vector<FontInfo> result;
  // Apple Color Emoji, resolved from the emoji probe, stores 'sbix' PNG
  // strikes and is handled by ColorBitmapFontLoader.
  for (unsigned int codepoint : kFallbackFontProbes) {
    FontInfo font = ResolveFontForCodepoint(codepoint);
    if (font.path.empty())
      continue;
    auto same = [&](const FontInfo& f) {
      return f.path == font.path && f.face_index == font.face_index;
    };
    if (std::find_if(result.begin(), result.end(), same) == result.end())
      result.push_back(std::move(font));
  }
  return result;
}

void Platform::OpenURL(const char* url) {
  NSString* str = [NSString stringWithUTF8String:url];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:str]];
}

void Platform::SetPrimarySelection(const char* /*text*/) {}
const char* Platform::GetPrimarySelection() {
  return "";
}

void Platform::BeginInteractiveMove() {
  NSWindow* nswindow = (NSWindow*)glfwGetCocoaWindow(window_);
  NSPoint loc = [nswindow mouseLocationOutsideOfEventStream];
  NSEvent* event =
      [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                         location:loc
                    modifierFlags:0
                        timestamp:[[NSProcessInfo processInfo] systemUptime]
                     windowNumber:[nswindow windowNumber]
                          context:nil
                      eventNumber:0
                       clickCount:1
                         pressure:1.0];
  [nswindow performWindowDragWithEvent:event];
}

void Platform::ShowWindowMenu(int /*x*/, int /*y*/) {}

Platform::CSDButtonVisibility Platform::GetCSDButtonVisibility() const {
  return {};
}

void Platform::BeginInteractiveResize(int /*edges*/) {
  // macOS handles resize natively via NSWindowStyleMaskResizable even for
  // borderless windows, so no custom interactive resize is needed.
}

void Platform::SetWindowIcon(int width,
                             int height,
                             const unsigned char* rgba_pixels) {
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:const_cast<unsigned char**>(&rgba_pixels)
                    pixelsWide:width
                    pixelsHigh:height
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                   bytesPerRow:width * 4
                  bitsPerPixel:32];
  NSImage* icon = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
  [icon addRepresentation:rep];
  [NSApp setApplicationIconImage:icon];
  [icon release];
  [rep release];
}

}  // namespace eng
