#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>

#include "third_party/kaliber/base/log.h"

namespace eng {

namespace {

std::filesystem::path GetXdgDataDir() {
  namespace fs = std::filesystem;
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && xdg[0] != '\0')
    return xdg;
  const char* home = std::getenv("HOME");
  if (home)
    return fs::path(home) / ".local" / "share";
  return {};
}

}  // namespace

void InstallDesktopEntry(std::span<const unsigned char> icon_data) {
  namespace fs = std::filesystem;

  fs::path data_dir = GetXdgDataDir();
  if (data_dir.empty()) {
    DLOG(0) << "Could not determine XDG data directory.";
    return;
  }

  // Install icon extracted from embedded PNG data.
  fs::path icon_dir = data_dir / "icons" / "hicolor" / "256x256" / "apps";
  fs::path icon_path = icon_dir / "gel.png";
  {
    std::error_code ec;
    fs::create_directories(icon_dir, ec);
    if (ec) {
      DLOG(0) << "Failed to create " << icon_dir << ": " << ec.message();
    } else {
      std::ofstream icon_file(icon_path, std::ios::binary);
      if (!icon_file) {
        DLOG(0) << "Failed to write " << icon_path;
      } else {
        icon_file.write(reinterpret_cast<const char*>(icon_data.data()),
                        icon_data.size());
        DLOG(0) << "Installed " << icon_path;
      }
    }
  }

  // Install .desktop file.
  fs::path app_dir = data_dir / "applications";
  fs::path desktop_path = app_dir / "gel.desktop";
  {
    std::error_code ec;
    fs::create_directories(app_dir, ec);
    if (ec) {
      DLOG(0) << "Failed to create " << app_dir << ": " << ec.message();
    } else {
      std::ofstream desktop_file(desktop_path);
      if (!desktop_file) {
        DLOG(0) << "Failed to write " << desktop_path;
      } else {
        std::error_code exe_ec;
        fs::path exe_path = fs::canonical("/proc/self/exe", exe_ec);
        if (exe_ec)
          exe_path = "gel";
        desktop_file << "[Desktop Entry]\n"
                     << "Type=Application\n"
                     << "Name=Gel\n"
                     << "Comment=Graphical repository browser\n"
                     << "Exec=" << exe_path.string() << "\n"
                     << "Icon=gel\n"
                     << "Terminal=false\n"
                     << "StartupWMClass=gel\n"
                     << "Categories=Development;RevisionControl;\n";
        DLOG(0) << "Installed " << desktop_path;
      }
    }
  }
}

void UninstallDesktopEntry() {
  namespace fs = std::filesystem;

  fs::path data_dir = GetXdgDataDir();
  if (data_dir.empty()) {
    DLOG(0) << "Could not determine XDG data directory.";
    return;
  }

  std::error_code ec;
  fs::path icon_path =
      data_dir / "icons" / "hicolor" / "256x256" / "apps" / "gel.png";
  if (fs::remove(icon_path, ec))
    DLOG(0) << "Removed " << icon_path;
  else if (ec)
    DLOG(0) << "Failed to remove " << icon_path << ": " << ec.message();

  fs::path desktop_path = data_dir / "applications" / "gel.desktop";
  if (fs::remove(desktop_path, ec))
    DLOG(0) << "Removed " << desktop_path;
  else if (ec)
    DLOG(0) << "Failed to remove " << desktop_path << ": " << ec.message();
}

}  // namespace eng
