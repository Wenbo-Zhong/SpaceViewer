#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "app.hpp"
#include "host.hpp"
#include "menu.hpp"
#include "text.hpp"

#include "xdg-decoration-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

struct Wayland;

void clearEdges(Input& input) {
  input.leftPressed = false;
  input.rightPressed = false;
  input.keyBackspace = false;
  input.keyDelete = false;
  input.keyEnter = false;
  input.keyTab = false;
  input.keyEsc = false;
  input.keyLeft = false;
  input.keyRight = false;
  input.keyHome = false;
  input.keyEnd = false;
  input.keyF5 = false;
  input.keyUpNav = false;
  input.keyBackNav = false;
  input.keyForwardNav = false;
  input.copy = false;
  input.paste = false;
  input.selectAll = false;
  input.openShortcut = false;
  input.typed.clear();
}

struct ShmBuffer {
  Wayland* app = nullptr;
  void* map = nullptr;
  size_t bytes = 0;
  wl_buffer* buffer = nullptr;
};

struct Wayland {
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  wl_output* output = nullptr;
  wl_data_device_manager* dataManager = nullptr;
  wl_data_device* dataDevice = nullptr;
  wl_data_offer* offer = nullptr;
  wl_data_source* dataSource = nullptr;
  xdg_wm_base* wm = nullptr;
  zxdg_decoration_manager_v1* decorationManager = nullptr;
  zxdg_toplevel_decoration_v1* decoration = nullptr;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  xkb_context* xkb = nullptr;
  xkb_keymap* keymap = nullptr;
  xkb_state* xkbState = nullptr;
  FT_Library ft = nullptr;
  FT_Face face = nullptr;
  FT_Face cjkFace = nullptr;
  std::string fontPath;
  std::string cjkFontPath;
  int facePx = 0;
  int cjkFacePx = 0;
  int scale = 1;
  int logicalW = 1100;
  int logicalH = 740;
  int wakePipe[2] = {-1, -1};
  uint32_t serial = 0;
  bool configured = false;
  bool running = true;
  bool dirty = true;
  bool offerUtf8 = false;
  std::string clipText;
  bool clipMine = false;
  App app;
  Input input;
  LinuxMenuState menu;
};

int outputScale(Wayland& app) {
  if (const char* env = std::getenv("GDK_SCALE")) {
    int factor = std::atoi(env);
    if (factor >= 1 && factor <= 4 && app.scale < factor) return factor;
  }
  return app.scale < 1 ? 1 : app.scale;
}

void seatCapabilities(void* data, wl_seat* seat, uint32_t caps);
void seatName(void*, wl_seat*, const char*) {}

void pointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y);
void pointerLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}
void pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y);
void pointerButton(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state);
void pointerAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void pointerFrame(void*, wl_pointer*) {}
void pointerAxisSource(void*, wl_pointer*, uint32_t) {}
void pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

void keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size);
void keyboardEnter(void* data, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*) ;
void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
void keyboardKey(void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state);
void keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
void keyboardRepeat(void*, wl_keyboard*, int32_t, int32_t) {}

void outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void outputDone(void*, wl_output*) {}
void outputScale(void* data, wl_output*, int32_t factor);

void dataOfferOffer(void* data, wl_data_offer* offer, const char* mime);
void dataOfferSourceActions(void*, wl_data_offer*, uint32_t) {}
void dataOfferAction(void*, wl_data_offer*, uint32_t) {}
void dataDeviceOffer(void* data, wl_data_device*, wl_data_offer* offer);
void dataDeviceEnter(void*, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*) {}
void dataDeviceLeave(void*, wl_data_device*) {}
void dataDeviceMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}
void dataDeviceDrop(void*, wl_data_device*) {}
void dataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer);
void dataSourceTarget(void*, wl_data_source*, const char*) {}
void dataSourceSend(void* data, wl_data_source*, const char* mime, int32_t fd);
void dataSourceCancelled(void* data, wl_data_source* source);
void dataSourceDrop(void*, wl_data_source*) {}
void dataSourceFinished(void*, wl_data_source*) {}
void dataSourceAction(void*, wl_data_source*, uint32_t) {}

void wmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
void xdgConfigure(void* data, xdg_surface* surface, uint32_t serial);
void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states);
void toplevelClose(void* data, xdg_toplevel*);

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
void registryRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistry = {registryGlobal, registryRemove};
const wl_seat_listener kSeat = {seatCapabilities, seatName};
const wl_pointer_listener kPointer = {pointerEnter, pointerLeave, pointerMotion, pointerButton, pointerAxis,
                                      pointerFrame, pointerAxisSource, pointerAxisStop, pointerAxisDiscrete};
const wl_keyboard_listener kKeyboard = {keyboardKeymap, keyboardEnter, keyboardLeave, keyboardKey, keyboardModifiers,
                                        keyboardRepeat};
const wl_output_listener kOutput = {outputGeometry, outputMode, outputDone, outputScale};
const wl_data_offer_listener kOffer = {dataOfferOffer, dataOfferSourceActions, dataOfferAction};
const wl_data_device_listener kDataDevice = {dataDeviceOffer, dataDeviceEnter, dataDeviceLeave, dataDeviceMotion,
                                            dataDeviceDrop, dataDeviceSelection};
const wl_data_source_listener kDataSource = {dataSourceTarget, dataSourceSend, dataSourceCancelled, dataSourceDrop,
                                            dataSourceFinished, dataSourceAction};
const xdg_wm_base_listener kWm = {wmPing};
const xdg_surface_listener kXdgSurface = {xdgConfigure};
const xdg_toplevel_listener kToplevel = {toplevelConfigure, toplevelClose};

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
  auto* app = static_cast<Wayland*>(data);
  if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
    app->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    app->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
    app->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    wl_seat_add_listener(app->seat, &kSeat, app);
  } else if (std::strcmp(interface, wl_output_interface.name) == 0 && !app->output) {
    app->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 2u)));
    wl_output_add_listener(app->output, &kOutput, app);
  } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    app->wm = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 3u)));
    xdg_wm_base_add_listener(app->wm, &kWm, app);
  } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
    app->decorationManager = static_cast<zxdg_decoration_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
  } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    app->dataManager = static_cast<wl_data_device_manager*>(
        wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
  }
}

void outputScale(void* data, wl_output*, int32_t factor) {
  auto* app = static_cast<Wayland*>(data);
  if (factor >= 1 && factor <= 4) {
    app->scale = factor;
    app->dirty = true;
  }
}

void seatCapabilities(void* data, wl_seat* seat, uint32_t caps) {
  auto* app = static_cast<Wayland*>(data);
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
    app->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app->pointer, &kPointer, app);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
    app->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app->keyboard, &kKeyboard, app);
  }
}

void pointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
  auto* app = static_cast<Wayland*>(data);
  app->serial = serial;
  int scale = outputScale(*app);
  app->input.mouseX = wl_fixed_to_int(x) * scale;
  app->input.mouseY = wl_fixed_to_int(y) * scale;
  app->dirty = true;
}

void pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
  auto* app = static_cast<Wayland*>(data);
  int scale = outputScale(*app);
  app->input.mouseX = wl_fixed_to_int(x) * scale;
  app->input.mouseY = wl_fixed_to_int(y) * scale;
  app->dirty = true;
}

void pointerButton(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
  auto* app = static_cast<Wayland*>(data);
  app->serial = serial;
  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  if (button == 0x110) {
    if (app->input.mouseY < linuxTitlebarHeight(app->input.dpi) &&
        linuxTitlebarDragHit(app->input.mouseX, app->input.mouseY, app->input.windowW, app->input.dpi)) {
      if (app->toplevel && app->seat) xdg_toplevel_move(app->toplevel, app->seat, serial);
      app->dirty = true;
      return;
    }
    app->input.leftPressed = true;
  }
  else if (button == 0x111) app->input.rightPressed = true;
  app->dirty = true;
}

void keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto* app = static_cast<Wayland*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map == MAP_FAILED) return;
  if (app->xkbState) xkb_state_unref(app->xkbState);
  if (app->keymap) xkb_keymap_unref(app->keymap);
  app->keymap = xkb_keymap_new_from_string(app->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map, size);
  app->xkbState = app->keymap ? xkb_state_new(app->keymap) : nullptr;
}

void keyboardEnter(void* data, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*) {
  static_cast<Wayland*>(data)->serial = serial;
}

void keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                       uint32_t group) {
  auto* app = static_cast<Wayland*>(data);
  if (app->xkbState) xkb_state_update_mask(app->xkbState, depressed, latched, locked, 0, 0, group);
}

bool modActive(xkb_state* state, const char* name) {
  return state && xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) == 1;
}

void keyboardKey(void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
  auto* app = static_cast<Wayland*>(data);
  app->serial = serial;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !app->xkbState) return;
  xkb_keysym_t sym = xkb_state_key_get_one_sym(app->xkbState, key + 8);
  bool ctrl = modActive(app->xkbState, XKB_MOD_NAME_CTRL);
  bool alt = modActive(app->xkbState, XKB_MOD_NAME_ALT);
  app->input.modCtrl = ctrl;
  app->input.modAlt = alt;
  if (ctrl && (sym == XKB_KEY_c || sym == XKB_KEY_C)) app->input.copy = true;
  else if (ctrl && (sym == XKB_KEY_v || sym == XKB_KEY_V)) app->input.paste = true;
  else if (ctrl && (sym == XKB_KEY_a || sym == XKB_KEY_A)) app->input.selectAll = true;
  else if (ctrl && (sym == XKB_KEY_o || sym == XKB_KEY_O)) app->input.openShortcut = true;
  else if (sym == XKB_KEY_F5) app->input.keyF5 = true;
  else if (sym == XKB_KEY_BackSpace) app->input.keyBackspace = true;
  else if (sym == XKB_KEY_Delete) app->input.keyDelete = true;
  else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) app->input.keyEnter = true;
  else if (sym == XKB_KEY_Tab) app->input.keyTab = true;
  else if (sym == XKB_KEY_Escape) app->input.keyEsc = true;
  else if (sym == XKB_KEY_Left) app->input.keyLeft = alt ? (app->input.keyBackNav = true, false) : true;
  else if (sym == XKB_KEY_Right) app->input.keyRight = alt ? (app->input.keyForwardNav = true, false) : true;
  else if (sym == XKB_KEY_Up && alt) app->input.keyUpNav = true;
  else if (sym == XKB_KEY_Home) app->input.keyHome = true;
  else if (sym == XKB_KEY_End) app->input.keyEnd = true;
  else if (!ctrl) {
    char utf8[32];
    int length = xkb_state_key_get_utf8(app->xkbState, key + 8, utf8, sizeof(utf8));
    if (length > 0) app->input.typed.append(utf8, static_cast<size_t>(length));
  }
  app->dirty = true;
}

void dataOfferOffer(void* data, wl_data_offer*, const char* mime) {
  auto* app = static_cast<Wayland*>(data);
  if (std::strcmp(mime, "text/plain;charset=utf-8") == 0 || std::strcmp(mime, "text/plain") == 0 ||
      std::strcmp(mime, "UTF8_STRING") == 0) {
    app->offerUtf8 = true;
  }
}

void dataDeviceOffer(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* app = static_cast<Wayland*>(data);
  app->offerUtf8 = false;
  wl_data_offer_add_listener(offer, &kOffer, app);
}

void dataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* app = static_cast<Wayland*>(data);
  if (app->offer && app->offer != offer) wl_data_offer_destroy(app->offer);
  app->offer = offer;
  app->clipMine = false;
}

void dataSourceSend(void* data, wl_data_source*, const char*, int32_t fd) {
  auto* app = static_cast<Wayland*>(data);
  if (!app->clipText.empty()) {
    ssize_t wrote = write(fd, app->clipText.data(), app->clipText.size());
    (void)wrote;
  }
  close(fd);
}

void dataSourceCancelled(void* data, wl_data_source* source) {
  auto* app = static_cast<Wayland*>(data);
  if (app->dataSource == source) app->dataSource = nullptr;
  wl_data_source_destroy(source);
  app->clipMine = false;
}

void xdgConfigure(void* data, xdg_surface* surface, uint32_t serial) {
  auto* app = static_cast<Wayland*>(data);
  xdg_surface_ack_configure(surface, serial);
  app->configured = true;
  app->dirty = true;
}

void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states) {
  auto* app = static_cast<Wayland*>(data);
  if (width > 0) app->logicalW = width;
  if (height > 0) app->logicalH = height;
  app->menu.maximized = false;
  if (states) {
    auto* state = static_cast<const uint32_t*>(states->data);
    const auto* end = reinterpret_cast<const uint32_t*>(
        static_cast<const char*>(states->data) + states->size);
    for (; state < end; ++state) {
      if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED) app->menu.maximized = true;
    }
  }
  app->dirty = true;
}

void toplevelClose(void* data, xdg_toplevel*) { static_cast<Wayland*>(data)->running = false; }

std::string bundledFontPath(const char* filename) {
  char executable[4096] = {};
  ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  std::string path = length > 0 ? std::string(executable, static_cast<size_t>(length)) : std::string(".");
  size_t slash = path.find_last_of('/');
  path = slash == std::string::npos ? "." : path.substr(0, slash);
  return path + "/fonts/" + filename;
}

bool traditionalChinese() {
  return zh::language() == zh::Language::TraditionalChinese;
}

bool loadFace(Wayland& app, int px, bool cjk = false) {
  if (!app.ft && FT_Init_FreeType(&app.ft) != 0) return false;
  if (app.fontPath.empty() || app.cjkFontPath.empty()) {
    FcInit();
    FcConfig* config = FcConfigGetCurrent();
    if (config) {
      for (const char* filename : {"Inter-Regular.ttf", "SourceHanSansSC-Regular.otf", "SourceHanSansTC-Regular.otf"}) {
        std::string path = bundledFontPath(filename);
        FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(path.c_str()));
      }
      FcConfigBuildFonts(config);
    }
    app.fontPath = bundledFontPath("Inter-Regular.ttf");
    app.cjkFontPath = bundledFontPath(traditionalChinese() ? "SourceHanSansTC-Regular.otf" : "SourceHanSansSC-Regular.otf");
  }
  FT_Face& face = cjk ? app.cjkFace : app.face;
  const std::string& path = cjk ? app.cjkFontPath : app.fontPath;
  if (path.empty()) return false;
  if (!face && FT_New_Face(app.ft, path.c_str(), 0, &face) != 0) return false;
  int& facePx = cjk ? app.cjkFacePx : app.facePx;
  if (facePx != px) {
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
    facePx = px;
  }
  return true;
}

bool isChinese(uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
         (cp >= 0xFF00 && cp <= 0xFFEF);
}

uint32_t decodeUtf8(const std::string& text, size_t& index) {
  if (index >= text.size()) return 0;
  auto byte = static_cast<unsigned char>(text[index++]);
  if (byte < 0x80) return byte;
  int need = byte >= 0xf0 ? 3 : byte >= 0xe0 ? 2 : byte >= 0xc0 ? 1 : 0;
  uint32_t cp = byte & (need == 3 ? 0x07 : need == 2 ? 0x0f : 0x1f);
  while (need-- > 0 && index < text.size()) {
    cp = (cp << 6) | (static_cast<unsigned char>(text[index++]) & 0x3f);
  }
  return cp;
}

void blendGlyph(uint32_t* pixels, int frameW, int frameH, FT_Bitmap& bitmap, int left, int top, uint32_t rgb,
                int clipX, int clipY, int clipW, int clipH) {
  int cr = (rgb >> 16) & 255;
  int cg = (rgb >> 8) & 255;
  int cb = rgb & 255;
  for (unsigned row = 0; row < bitmap.rows; ++row) {
    int y = top + static_cast<int>(row);
    if (y < clipY || y >= clipY + clipH || y < 0 || y >= frameH) continue;
    for (unsigned col = 0; col < bitmap.width; ++col) {
      int x = left + static_cast<int>(col);
      if (x < clipX || x >= clipX + clipW || x < 0 || x >= frameW) continue;
      int cover = bitmap.buffer[row * bitmap.pitch + col];
      if (cover <= 0) continue;
      uint32_t* dst = pixels + static_cast<size_t>(y) * static_cast<size_t>(frameW) + x;
      int db = *dst & 255;
      int dg = (*dst >> 8) & 255;
      int dr = (*dst >> 16) & 255;
      int b = db + (cb - db) * cover / 255;
      int g = dg + (cg - dg) * cover / 255;
      int r = dr + (cr - dr) * cover / 255;
      *dst = 0xff000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
  }
}

void drawTexts(Wayland& app, Frame& frame) {
  for (const Frame::Text& text : frame.texts) {
    int px = std::max(8, text.fontPx);
    if (!loadFace(app, px)) return;
    std::vector<uint32_t> points;
    size_t index = 0;
    while (index < text.utf8.size()) points.push_back(decodeUtf8(text.utf8, index));
    int width = 0;
    for (uint32_t cp : points) {
      FT_Face face = isChinese(cp) ? app.cjkFace : app.face;
      if (!face || FT_Load_Char(face, cp, FT_LOAD_DEFAULT) != 0) continue;
      width += static_cast<int>(face->glyph->advance.x >> 6);
    }
    int ascent = static_cast<int>(app.face->size->metrics.ascender >> 6);
    int descent = static_cast<int>(-(app.face->size->metrics.descender >> 6));
    if (app.cjkFace) {
      ascent = std::max(ascent, static_cast<int>(app.cjkFace->size->metrics.ascender >> 6));
      descent = std::max(descent, static_cast<int>(-(app.cjkFace->size->metrics.descender >> 6)));
    }
    int x = text.x;
    if (text.center) x += std::max(0, (text.w - width) / 2);
    int baseline = text.y + std::max(0, (text.h - (ascent + descent)) / 2) + ascent;
    for (uint32_t cp : points) {
      FT_Face face = isChinese(cp) ? app.cjkFace : app.face;
      if (!face || FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0) continue;
      FT_GlyphSlot glyph = face->glyph;
      blendGlyph(frame.bgra.data(), frame.width, frame.height, glyph->bitmap, x + glyph->bitmap_left,
                 baseline - glyph->bitmap_top, text.rgb, text.x, text.y, text.w, text.h);
      x += static_cast<int>(glyph->advance.x >> 6);
    }
  }
}

void destroyBuffer(void* data, wl_buffer* buffer) {
  auto* shm = static_cast<ShmBuffer*>(data);
  if (shm->map) munmap(shm->map, shm->bytes);
  wl_buffer_destroy(buffer);
  delete shm;
}

const wl_buffer_listener kBuffer = {destroyBuffer};

bool present(Wayland& app, Frame& frame) {
  if (!app.shm || !app.surface || frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) return false;
  drawTexts(app, frame);
  int stride = frame.width * 4;
  size_t bytes = static_cast<size_t>(stride) * static_cast<size_t>(frame.height);
  int fd = memfd_create("spaceviewer", MFD_CLOEXEC);
  if (fd < 0) return false;
  if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
    close(fd);
    return false;
  }
  void* map = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return false;
  }
  std::memcpy(map, frame.bgra.data(), bytes);
  wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, static_cast<int32_t>(bytes));
  close(fd);
  wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, frame.width, frame.height, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  auto* shm = new ShmBuffer{&app, map, bytes, buffer};
  wl_buffer_add_listener(buffer, &kBuffer, shm);
  int scale = outputScale(app);
  wl_surface_set_buffer_scale(app.surface, scale);
  wl_surface_attach(app.surface, buffer, 0, 0);
  wl_surface_damage_buffer(app.surface, 0, 0, frame.width, frame.height);
  wl_surface_commit(app.surface);
  return true;
}

void setClipboard(Wayland& app, const std::string& text) {
  app.clipText = text;
  app.clipMine = true;
  if (!app.dataManager || !app.dataDevice) return;
  if (app.dataSource) {
    wl_data_source_destroy(app.dataSource);
    app.dataSource = nullptr;
  }
  app.dataSource = wl_data_device_manager_create_data_source(app.dataManager);
  wl_data_source_offer(app.dataSource, "text/plain;charset=utf-8");
  wl_data_source_offer(app.dataSource, "text/plain");
  wl_data_source_add_listener(app.dataSource, &kDataSource, &app);
  wl_data_device_set_selection(app.dataDevice, app.dataSource, app.serial);
}

std::string getClipboard(Wayland& app) {
  if (app.clipMine) return app.clipText;
  if (!app.offer || !app.offerUtf8) return {};
  int fds[2] = {-1, -1};
  if (pipe(fds) != 0) return {};
  wl_data_offer_receive(app.offer, "text/plain;charset=utf-8", fds[1]);
  close(fds[1]);
  wl_display_flush(app.display);
  std::string text;
  for (int i = 0; i < 30; ++i) {
    pollfd pfds[2]{};
    pfds[0].fd = wl_display_get_fd(app.display);
    pfds[0].events = POLLIN;
    pfds[1].fd = fds[0];
    pfds[1].events = POLLIN;
    poll(pfds, 2, 30);
    if (pfds[0].revents & POLLIN) wl_display_dispatch(app.display);
    if (pfds[1].revents & (POLLIN | POLLHUP)) {
      char buffer[1024];
      ssize_t n = 0;
      while ((n = read(fds[0], buffer, sizeof(buffer))) > 0) text.append(buffer, static_cast<size_t>(n));
      break;
    }
  }
  close(fds[0]);
  return text;
}

void scanArgument(App& app, int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != '-') {
      app.scanPath(argv[i]);
      return;
    }
  }
}

}  // namespace

int spaceviewer_wayland_main(int argc, char** argv) {
  Wayland app;
  app.display = wl_display_connect(nullptr);
  if (!app.display) return 2;
  app.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (pipe(app.wakePipe) != 0) {
    wl_display_disconnect(app.display);
    return 2;
  }
  fcntl(app.wakePipe[0], F_SETFL, O_NONBLOCK);
  wl_registry* registry = wl_display_get_registry(app.display);
  wl_registry_add_listener(registry, &kRegistry, &app);
  wl_display_roundtrip(app.display);
  wl_display_roundtrip(app.display);
  if (!app.compositor || !app.shm || !app.wm) {
    std::fprintf(stderr, "SpaceViewer: compositor is missing xdg-shell\n");
    wl_display_disconnect(app.display);
    return 2;
  }
  if (app.dataManager && app.seat) {
    app.dataDevice = wl_data_device_manager_get_data_device(app.dataManager, app.seat);
    wl_data_device_add_listener(app.dataDevice, &kDataDevice, &app);
  }
  app.surface = wl_compositor_create_surface(app.compositor);
  app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wm, app.surface);
  xdg_surface_add_listener(app.xdgSurface, &kXdgSurface, &app);
  app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
  xdg_toplevel_add_listener(app.toplevel, &kToplevel, &app);
  xdg_toplevel_set_title(app.toplevel, "SpaceViewer");
  xdg_toplevel_set_app_id(app.toplevel, "spaceviewer");
  if (app.decorationManager) {
    app.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(app.decorationManager, app.toplevel);
    static const zxdg_toplevel_decoration_v1_listener decorationListener = {
        [](void*, zxdg_toplevel_decoration_v1*, uint32_t) {}};
    zxdg_toplevel_decoration_v1_add_listener(app.decoration, &decorationListener, nullptr);
    zxdg_toplevel_decoration_v1_set_mode(app.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
  }
  wl_surface_commit(app.surface);
  wl_display_roundtrip(app.display);

  Host host;
  linuxFillHost(host);
  host.setClipboard = [&app](const std::string& text) { setClipboard(app, text); };
  host.getClipboard = [&app]() { return getClipboard(app); };
  host.setTitle = [&app](const std::string& title) {
    if (app.toplevel) xdg_toplevel_set_title(app.toplevel, title.c_str());
  };
  app.app.setWake([&app]() {
    if (app.wakePipe[1] >= 0) {
      char byte = 1;
      ssize_t wrote = write(app.wakePipe[1], &byte, 1);
      (void)wrote;
    }
  });
  scanArgument(app.app, argc, argv);

  auto paint = [&]() {
    int scale = outputScale(app);
    app.input.windowW = std::max(1, app.logicalW * scale);
    app.input.windowH = std::max(1, app.logicalH * scale);
    app.input.dpi = static_cast<float>(96 * scale);
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    app.input.timeSec = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
    Frame frame;
    Input uiInput = app.input;
    const int windowHeight = uiInput.windowH;
    linuxMenuPrepare(app.menu, uiInput);
    if (app.menu.closeRequested) app.running = false;
    if (app.menu.minimizeRequested) {
      app.menu.minimizeRequested = false;
      xdg_toplevel_set_minimized(app.toplevel);
    }
    if (app.menu.maximizeRequested) {
      app.menu.maximizeRequested = false;
      if (app.menu.maximized) xdg_toplevel_unset_maximized(app.toplevel);
      else xdg_toplevel_set_maximized(app.toplevel);
    }
    if (app.menu.rootScanRequested) {
      app.menu.rootScanRequested = false;
      if (!linuxLaunchRootScan()) std::fprintf(stderr, "SpaceViewer: cannot start pkexec root scan\n");
    }
    app.app.update(uiInput, host, frame);
    linuxMenuPresent(app.menu, frame, windowHeight, uiInput.dpi);
    present(app, frame);
    clearEdges(app.input);
    app.dirty = false;
  };

  while (app.running) {
    wl_display_flush(app.display);
    if (wl_display_prepare_read(app.display) != 0) {
      wl_display_dispatch_pending(app.display);
      continue;
    }
    pollfd pfds[2]{};
    pfds[0].fd = wl_display_get_fd(app.display);
    pfds[0].events = POLLIN;
    pfds[1].fd = app.wakePipe[0];
    pfds[1].events = POLLIN;
    int timeout = app.app.animating() ? 16 : app.dirty ? 0 : 200;
    int polled = poll(pfds, 2, timeout);
    if (polled > 0 && (pfds[0].revents & POLLIN)) {
      wl_display_read_events(app.display);
    } else {
      wl_display_cancel_read(app.display);
    }
    wl_display_dispatch_pending(app.display);
    if (pfds[1].revents & POLLIN) {
      char buffer[64];
      while (read(app.wakePipe[0], buffer, sizeof(buffer)) > 0) {
      }
      app.dirty = true;
    }
    if (app.configured && (app.dirty || app.app.animating())) paint();
    if (wl_display_get_error(app.display) != 0) {
      std::fprintf(stderr, "SpaceViewer: Wayland connection closed\n");
      break;
    }
  }

  if (app.face) FT_Done_Face(app.face);
  if (app.cjkFace) FT_Done_Face(app.cjkFace);
  if (app.ft) FT_Done_FreeType(app.ft);
  if (app.xkbState) xkb_state_unref(app.xkbState);
  if (app.keymap) xkb_keymap_unref(app.keymap);
  if (app.xkb) xkb_context_unref(app.xkb);
  if (app.wakePipe[0] >= 0) close(app.wakePipe[0]);
  if (app.wakePipe[1] >= 0) close(app.wakePipe[1]);
  wl_display_disconnect(app.display);
  return 0;
}
