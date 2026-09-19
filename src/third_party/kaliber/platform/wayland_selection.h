#ifndef ENGINE_PLATFORM_WAYLAND_SELECTION_H
#define ENGINE_PLATFORM_WAYLAND_SELECTION_H

#include <string>

#include <wayland-client.h>

#include "third_party/wayland-protocols/primary-selection-unstable-v1-client-protocol.h"

namespace eng {

struct WaylandSelection {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_seat* seat = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  // Primary selection (middle-click paste).
  zwp_primary_selection_device_manager_v1* ps_manager = nullptr;
  zwp_primary_selection_device_v1* ps_device = nullptr;
  zwp_primary_selection_source_v1* ps_source = nullptr;
  std::string ps_text;
  // Track incoming primary selection offer from external apps for lazy reading.
  zwp_primary_selection_offer_v1* ps_offer = nullptr;
  bool ps_offer_has_text = false;
  std::string ps_read_cache;
  bool ps_read_valid = false;
  // Clipboard. We manage our own wl_data_device because GLFW's
  // dataSourceHandleSend doesn't handle EAGAIN, truncating large payloads.
  wl_data_device_manager* cb_manager = nullptr;
  wl_data_device* cb_device = nullptr;
  wl_data_source* cb_source = nullptr;
  std::string cb_text;
  // Track incoming clipboard offer from external apps for lazy reading.
  wl_data_offer* cb_offer = nullptr;
  bool cb_offer_has_text = false;
  std::string cb_read_cache;
  bool cb_read_valid = false;

  uint32_t serial = 0;
};

// Initialize/shutdown Wayland selection handling (clipboard and primary
// selection). InitWaylandSelection() binds registry globals, creates a seat,
// and sets up listeners for clipboard and primary selection protocols.
void InitWaylandSelection();
void ShutdownWaylandSelection();

// Returns the global WaylandSelection state, or nullptr if not initialized.
WaylandSelection* GetWaylandSelection();

// Listeners needed by Platform clipboard/selection methods.
extern const wl_data_source_listener kCbSourceListener;
extern const zwp_primary_selection_source_v1_listener kPsSourceListener;

}  // namespace eng

#endif  // ENGINE_PLATFORM_WAYLAND_SELECTION_H
