#include "third_party/kaliber/platform/wayland_selection.h"

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <unistd.h>

#include "third_party/glfw/glfw/include/GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "third_party/glfw/glfw/include/GLFW/glfw3native.h"

namespace eng {

namespace {

// Wayland primary selection support via the zwp_primary_selection_v1 protocol.
// This allows selected text to be pasted with middle-click on Wayland. We bind
// our own wl_seat, wl_pointer, and wl_keyboard (separate from GLFW's) to track
// input event serials, which are required by set_selection to authorize the
// selection change. The source's "send" callback writes the stored text to the
// requesting client's fd when they middle-click paste.

WaylandSelection* g_wl = nullptr;

// Write all of |buf| (|len| bytes) to |fd|, handling EAGAIN via poll().
void WriteAllToFd(int fd, const char* buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pollfd pfd = {fd, POLLOUT, 0};
        if (poll(&pfd, 1, 5000) <= 0)
          break;
        continue;
      }
      break;
    }
    buf += n;
    len -= static_cast<size_t>(n);
  }
  close(fd);
}

// Primary selection source listener.
void PsSourceSend(void* data,
                  zwp_primary_selection_source_v1*,
                  const char*,
                  int32_t fd) {
  auto* ws = static_cast<WaylandSelection*>(data);
  WriteAllToFd(fd, ws->ps_text.c_str(), ws->ps_text.size());
}

void PsSourceCancelled(void* data, zwp_primary_selection_source_v1* source) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (ws->ps_source == source) {
    zwp_primary_selection_source_v1_destroy(source);
    ws->ps_source = nullptr;
  }
}

// Primary selection offer listener: tracks whether the offer contains text.
void PsOfferOffer(void* data,
                  zwp_primary_selection_offer_v1*,
                  const char* mime_type) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
      strcmp(mime_type, "text/plain") == 0 ||
      strcmp(mime_type, "UTF8_STRING") == 0) {
    ws->ps_offer_has_text = true;
  }
}

const zwp_primary_selection_offer_v1_listener kPsOfferListener = {
    PsOfferOffer,
};

// Primary selection device listener: tracks offers for reading.
void PsDeviceDataOffer(void* data,
                       zwp_primary_selection_device_v1*,
                       zwp_primary_selection_offer_v1* offer) {
  auto* ws = static_cast<WaylandSelection*>(data);
  ws->ps_offer_has_text = false;
  zwp_primary_selection_offer_v1_add_listener(offer, &kPsOfferListener, ws);
}

void PsDeviceSelection(void* data,
                       zwp_primary_selection_device_v1*,
                       zwp_primary_selection_offer_v1* offer) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (ws->ps_offer)
    zwp_primary_selection_offer_v1_destroy(ws->ps_offer);
  ws->ps_offer = nullptr;
  ws->ps_read_valid = false;

  if (!offer)
    return;

  // If we own the primary selection, we don't need the offer.
  if (ws->ps_source) {
    zwp_primary_selection_offer_v1_destroy(offer);
    return;
  }

  if (ws->ps_offer_has_text) {
    ws->ps_offer = offer;
  } else {
    zwp_primary_selection_offer_v1_destroy(offer);
  }
}

const zwp_primary_selection_device_v1_listener kPsDeviceListener = {
    PsDeviceDataOffer,
    PsDeviceSelection,
};

// Clipboard data source listener: sends clipboard text to requesting client.
void CbSourceSend(void* data, wl_data_source*, const char*, int32_t fd) {
  auto* ws = static_cast<WaylandSelection*>(data);
  WriteAllToFd(fd, ws->cb_text.c_str(), ws->cb_text.size());
}

void CbSourceCancelled(void* data, wl_data_source* source) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (ws->cb_source == source) {
    wl_data_source_destroy(source);
    ws->cb_source = nullptr;
  }
}

// Clipboard data offer listener: tracks whether the offer contains text.
void CbOfferOffer(void* data, wl_data_offer*, const char* mime_type) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
      strcmp(mime_type, "text/plain") == 0 ||
      strcmp(mime_type, "UTF8_STRING") == 0) {
    ws->cb_offer_has_text = true;
  }
}

const wl_data_offer_listener kCbOfferListener = {
    .offer = CbOfferOffer,
    .source_actions = [](void*, wl_data_offer*, uint32_t) {},
    .action = [](void*, wl_data_offer*, uint32_t) {},
};

// Clipboard data device listener: stores offers lazily (no reading here).
void CbDeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* ws = static_cast<WaylandSelection*>(data);
  // Store as pending; the selection callback will pick it up.
  // Reset text flag; CbOfferOffer will set it if text mime is advertised.
  ws->cb_offer_has_text = false;
  wl_data_offer_add_listener(offer, &kCbOfferListener, ws);
}

void CbDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* ws = static_cast<WaylandSelection*>(data);
  // Destroy previous offer if any.
  if (ws->cb_offer)
    wl_data_offer_destroy(ws->cb_offer);
  ws->cb_offer = nullptr;
  ws->cb_read_valid = false;

  if (!offer)
    return;

  // If we own the clipboard, we don't need the offer (reading from it would
  // deadlock because our send callback can't fire during dispatch).
  if (ws->cb_source) {
    wl_data_offer_destroy(offer);
    return;
  }

  if (ws->cb_offer_has_text) {
    ws->cb_offer = offer;
  } else {
    wl_data_offer_destroy(offer);
  }
}

void CbDeviceEnter(void*,
                   wl_data_device*,
                   uint32_t,
                   wl_surface*,
                   wl_fixed_t,
                   wl_fixed_t,
                   wl_data_offer*) {}
void CbDeviceLeave(void*, wl_data_device*) {}
void CbDeviceMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}
void CbDeviceDrop(void*, wl_data_device*) {}

const wl_data_device_listener kCbDeviceListener = {
    CbDeviceDataOffer, CbDeviceEnter, CbDeviceLeave,
    CbDeviceMotion,    CbDeviceDrop,  CbDeviceSelection,
};

// Pointer listener (track serial from button events).
void PsPointerEnter(void*,
                    wl_pointer*,
                    uint32_t,
                    wl_surface*,
                    wl_fixed_t,
                    wl_fixed_t) {}
void PsPointerLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}
void PsPointerMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {}
void PsPointerButton(void* data,
                     wl_pointer*,
                     uint32_t serial,
                     uint32_t,
                     uint32_t,
                     uint32_t) {
  static_cast<WaylandSelection*>(data)->serial = serial;
}
void PsPointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void PsPointerFrame(void*, wl_pointer*) {}
void PsPointerAxisSource(void*, wl_pointer*, uint32_t) {}
void PsPointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void PsPointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
void PsPointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
void PsPointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

const wl_pointer_listener kPsPointerListener = {
    PsPointerEnter,
    PsPointerLeave,
    PsPointerMotion,
    PsPointerButton,
    PsPointerAxis,
    PsPointerFrame,
    PsPointerAxisSource,
    PsPointerAxisStop,
    PsPointerAxisDiscrete,
    PsPointerAxisValue120,
    PsPointerAxisRelativeDirection,
};

// Keyboard listener (track serial from key/enter events).
void PsKeyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t) {
  close(fd);
}
void PsKeyboardEnter(void* data,
                     wl_keyboard*,
                     uint32_t serial,
                     wl_surface*,
                     wl_array*) {
  static_cast<WaylandSelection*>(data)->serial = serial;
}
void PsKeyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
void PsKeyboardKey(void* data,
                   wl_keyboard*,
                   uint32_t serial,
                   uint32_t,
                   uint32_t,
                   uint32_t) {
  static_cast<WaylandSelection*>(data)->serial = serial;
}
void PsKeyboardModifiers(void*,
                         wl_keyboard*,
                         uint32_t,
                         uint32_t,
                         uint32_t,
                         uint32_t,
                         uint32_t) {}
void PsKeyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

const wl_keyboard_listener kPsKeyboardListener = {
    PsKeyboardKeymap, PsKeyboardEnter,     PsKeyboardLeave,
    PsKeyboardKey,    PsKeyboardModifiers, PsKeyboardRepeatInfo,
};

// Seat listener.
void PsSeatCapabilities(void* data, wl_seat* seat, uint32_t caps) {
  auto* ps = static_cast<WaylandSelection*>(data);
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ps->pointer) {
    ps->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(ps->pointer, &kPsPointerListener, ps);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ps->keyboard) {
    ps->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(ps->keyboard, &kPsKeyboardListener, ps);
  }
}
void PsSeatName(void*, wl_seat*, const char*) {}

const wl_seat_listener kPsSeatListener = {
    PsSeatCapabilities,
    PsSeatName,
};

// Registry listener.
void PsRegistryGlobal(void* data,
                      wl_registry* registry,
                      uint32_t name,
                      const char* interface,
                      uint32_t version) {
  auto* ws = static_cast<WaylandSelection*>(data);
  if (strcmp(interface,
             zwp_primary_selection_device_manager_v1_interface.name) == 0) {
    ws->ps_manager =
        static_cast<zwp_primary_selection_device_manager_v1*>(wl_registry_bind(
            registry, name, &zwp_primary_selection_device_manager_v1_interface,
            1));
  } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    ws->cb_manager = static_cast<wl_data_device_manager*>(
        wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3));
  } else if (strcmp(interface, "wl_seat") == 0 && !ws->seat) {
    uint32_t bind_ver = version < 5 ? version : 5;
    ws->seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, bind_ver));
    wl_seat_add_listener(ws->seat, &kPsSeatListener, ws);
  }
}
void PsRegistryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kPsRegistryListener = {
    PsRegistryGlobal,
    PsRegistryGlobalRemove,
};

}  // namespace

const zwp_primary_selection_source_v1_listener kPsSourceListener = {
    PsSourceSend,
    PsSourceCancelled,
};

const wl_data_source_listener kCbSourceListener = {
    .target = [](void*, wl_data_source*, const char*) {},
    .send = CbSourceSend,
    .cancelled = CbSourceCancelled,
    .dnd_drop_performed = [](void*, wl_data_source*) {},
    .dnd_finished = [](void*, wl_data_source*) {},
    .action = [](void*, wl_data_source*, uint32_t) {},
};

// Initialize Wayland selection handling. Gets the wl_display from GLFW and
// creates a separate registry to bind the primary selection manager,
// wl_data_device_manager (for clipboard), and a seat. Two roundtrips are
// needed: the first binds globals, the second processes seat capabilities
// (which creates pointer/keyboard for serial tracking).
void InitWaylandSelection() {
  auto* ws = new WaylandSelection();
  ws->display = glfwGetWaylandDisplay();
  if (!ws->display) {
    delete ws;
    return;
  }
  ws->registry = wl_display_get_registry(ws->display);
  wl_registry_add_listener(ws->registry, &kPsRegistryListener, ws);
  wl_display_roundtrip(ws->display);
  wl_display_roundtrip(ws->display);
  if (ws->seat) {
    if (ws->ps_manager) {
      ws->ps_device = zwp_primary_selection_device_manager_v1_get_device(
          ws->ps_manager, ws->seat);
      zwp_primary_selection_device_v1_add_listener(ws->ps_device,
                                                   &kPsDeviceListener, ws);
    }
    if (ws->cb_manager) {
      ws->cb_device =
          wl_data_device_manager_get_data_device(ws->cb_manager, ws->seat);
      wl_data_device_add_listener(ws->cb_device, &kCbDeviceListener, ws);
    }
    wl_display_roundtrip(ws->display);
    g_wl = ws;
  } else {
    if (ws->pointer)
      wl_pointer_destroy(ws->pointer);
    if (ws->keyboard)
      wl_keyboard_destroy(ws->keyboard);
    if (ws->ps_manager)
      zwp_primary_selection_device_manager_v1_destroy(ws->ps_manager);
    if (ws->cb_manager)
      wl_data_device_manager_destroy(ws->cb_manager);
    wl_registry_destroy(ws->registry);
    delete ws;
  }
}

void ShutdownWaylandSelection() {
  if (!g_wl)
    return;
  if (g_wl->cb_offer)
    wl_data_offer_destroy(g_wl->cb_offer);
  if (g_wl->cb_source)
    wl_data_source_destroy(g_wl->cb_source);
  if (g_wl->cb_device)
    wl_data_device_destroy(g_wl->cb_device);
  if (g_wl->cb_manager)
    wl_data_device_manager_destroy(g_wl->cb_manager);
  if (g_wl->ps_offer)
    zwp_primary_selection_offer_v1_destroy(g_wl->ps_offer);
  if (g_wl->ps_source)
    zwp_primary_selection_source_v1_destroy(g_wl->ps_source);
  if (g_wl->ps_device)
    zwp_primary_selection_device_v1_destroy(g_wl->ps_device);
  if (g_wl->ps_manager)
    zwp_primary_selection_device_manager_v1_destroy(g_wl->ps_manager);
  if (g_wl->pointer)
    wl_pointer_destroy(g_wl->pointer);
  if (g_wl->keyboard)
    wl_keyboard_destroy(g_wl->keyboard);
  if (g_wl->seat)
    wl_seat_destroy(g_wl->seat);
  wl_registry_destroy(g_wl->registry);
  delete g_wl;
  g_wl = nullptr;
}

WaylandSelection* GetWaylandSelection() {
  return g_wl;
}

}  // namespace eng
