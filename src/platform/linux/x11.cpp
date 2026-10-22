#include "app.hpp"
#include "host.hpp"
#include "menu.hpp"
#include "dpi.hpp"
#include "text.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

#include <cerrno>
#include <clocale>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <sys/select.h>

#include <unistd.h>

namespace {

int readDpi(Display* display, bool physicalDensity = true) {
  int dpi = 96;
  char* resource = XResourceManagerString(display);
  if (resource) {
    XrmInitialize();
    XrmDatabase db = XrmGetStringDatabase(resource);
    if (db) {
      XrmValue value;
      char* type = nullptr;
      if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) dpi = std::atoi(value.addr);
      XrmDestroyDatabase(db);
    }
  }
  if (dpi < 72) dpi = 96;
  if (dpi == 96) {
    if (const char* scale = std::getenv("GDK_SCALE")) {
      int factor = std::atoi(scale);
      if (factor >= 1 && factor <= 4) dpi = 96 * factor;
    }
  }
  if (!physicalDensity) return std::clamp(dpi, 96, 384);
  const int screen = DefaultScreen(display);
  const double density = linuxDensityScale(DisplayWidth(display, screen), DisplayHeight(display, screen),
                                           DisplayWidthMM(display, screen), DisplayHeightMM(display, screen));
  return std::clamp(std::max(dpi, static_cast<int>(96 * density)), 96, 384);
}

struct X11App {
  Display* display = nullptr;
  int screen = 0;
  Window window = 0;
  GC gc = nullptr;
  XftDraw* draw = nullptr;
  Colormap colormap = 0;
  Atom wmDelete{};
  Atom clipboard{};
  Atom utf8{};
  Atom targets{};
  Atom prop{};
  std::string clipText;
  int width = 1100;
  int height = 740;
  int dpi = 96;
  bool running = true;
  App app;
  Input input;
  LinuxMenuState menu;
  std::vector<XftFont*> fonts;
  std::vector<int> fontPx;
  std::vector<XftFont*> cjkFonts;
  std::vector<int> cjkFontPx;
  XIM xim = nullptr;
  XIC xic = nullptr;
};

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

bool isChinese(uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
         (cp >= 0xFF00 && cp <= 0xFFEF);
}

bool traditionalChinese() {
  return zh::language() == zh::Language::TraditionalChinese;
}

XftFont* fontFor(X11App& app, int px, bool cjk = false) {
  if (px < 6) px = 6;
  auto& sizes = cjk ? app.cjkFontPx : app.fontPx;
  auto& fonts = cjk ? app.cjkFonts : app.fonts;
  for (size_t i = 0; i < sizes.size(); ++i) {
    if (sizes[i] == px) return fonts[i];
  }
  std::string family = cjk ? (traditionalChinese() ? "Source Han Sans TC" : "Source Han Sans SC") : "Inter";
  std::string name = family + ":pixelsize=" + std::to_string(px);
  XftFont* font = XftFontOpenName(app.display, app.screen, name.c_str());
  fonts.push_back(font);
  sizes.push_back(px);
  return font;
}

std::vector<std::pair<std::string, bool>> splitRuns(const std::string& text) {
  std::vector<std::pair<std::string, bool>> runs;
  size_t i = 0;
  while (i < text.size()) {
    size_t start = i;
    unsigned char lead = static_cast<unsigned char>(text[i++]);
    int extra = lead < 0x80 ? 0 : lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    uint32_t cp = lead & (extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F);
    for (int j = 0; j < extra && i < text.size(); ++j) cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
    bool cjk = isChinese(cp);
    while (i < text.size()) {
      size_t next = i;
      unsigned char b = static_cast<unsigned char>(text[next++]);
      int n = b < 0x80 ? 0 : b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
      uint32_t nextCp = b & (n == 3 ? 7 : n == 2 ? 15 : n == 1 ? 31 : 127);
      for (int j = 0; j < n && next < text.size(); ++j) nextCp = (nextCp << 6) | (static_cast<unsigned char>(text[next++]) & 63);
      if (isChinese(nextCp) != cjk) break;
      i = next;
    }
    runs.emplace_back(text.substr(start, i - start), cjk);
  }
  return runs;
}

void drawTexts(X11App& app, const Frame& frame) {
  if (!app.draw) return;
  for (const Frame::Text& text : frame.texts) {
    auto runs = splitRuns(text.utf8);
    XRenderColor rc;
    rc.red = static_cast<unsigned short>(((text.rgb >> 16) & 255) * 257);
    rc.green = static_cast<unsigned short>(((text.rgb >> 8) & 255) * 257);
    rc.blue = static_cast<unsigned short>((text.rgb & 255) * 257);
    rc.alpha = 65535;
    XftColor color;
    if (!XftColorAllocValue(app.display, DefaultVisual(app.display, app.screen), app.colormap, &rc, &color)) continue;
    int px = std::max(6, text.fontPx);
    int totalWidth = 0, height = 0, ascent = 0, inkLeft = 0, inkRight = 0;
    auto measure = [&]() {
      totalWidth = 0;
      height = 0;
      ascent = 0;
      inkLeft = 0;
      inkRight = 0;
      bool hasInk = false;
      for (const auto& run : runs) {
        XftFont* font = fontFor(app, px, run.second);
        if (!font) continue;
        XGlyphInfo extent{};
        XftTextExtentsUtf8(app.display, font, reinterpret_cast<const FcChar8*>(run.first.data()), static_cast<int>(run.first.size()), &extent);
        const int left = totalWidth + extent.x;
        const int right = left + extent.width;
        if (extent.width > 0) {
          if (!hasInk) { inkLeft = left; inkRight = right; hasInk = true; }
          else { inkLeft = std::min(inkLeft, left); inkRight = std::max(inkRight, right); }
        }
        totalWidth += extent.xOff;
        height = std::max(height, font->ascent + font->descent);
        ascent = std::max(ascent, font->ascent);
      }
    };
    measure();
    while (height > text.h && px > 6) { --px; measure(); }
    int x = text.x;
    if (text.center) x += std::max(0, (text.w - (inkRight - inkLeft)) / 2) - inkLeft;
    int y = text.y + std::max(0, (text.h - height) / 2) + ascent;
    Region region = XCreateRegion();
    XRectangle rectangle{static_cast<short>(text.x), static_cast<short>(text.y), static_cast<unsigned short>(text.w),
                         static_cast<unsigned short>(text.h)};
    XUnionRectWithRegion(&rectangle, region, region);
    XftDrawSetClip(app.draw, region);
    for (const auto& run : runs) {
      XftFont* font = fontFor(app, px, run.second);
      if (!font) continue;
      XGlyphInfo extent{};
      XftTextExtentsUtf8(app.display, font, reinterpret_cast<const FcChar8*>(run.first.data()), static_cast<int>(run.first.size()), &extent);
      XftDrawStringUtf8(app.draw, &color, font, x, y, reinterpret_cast<const FcChar8*>(run.first.data()), static_cast<int>(run.first.size()));
      x += extent.xOff;
    }
    XftDrawSetClip(app.draw, nullptr);
    XDestroyRegion(region);
    XftColorFree(app.display, DefaultVisual(app.display, app.screen), app.colormap, &color);
  }
}

void present(X11App& app, const Frame& frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) return;
  Visual* visual = DefaultVisual(app.display, app.screen);
  XImage* image = XCreateImage(app.display, visual, 24, ZPixmap, 0, reinterpret_cast<char*>(const_cast<uint32_t*>(frame.bgra.data())),
                               frame.width, frame.height, 32, 0);
  if (!image) return;
  image->byte_order = LSBFirst;
  image->bitmap_bit_order = LSBFirst;
  image->bits_per_pixel = 32;
  image->red_mask = 0x00ff0000;
  image->green_mask = 0x0000ff00;
  image->blue_mask = 0x000000ff;
  XPutImage(app.display, app.window, app.gc, image, 0, 0, 0, 0, frame.width, frame.height);
  image->data = nullptr;
  XDestroyImage(image);
  drawTexts(app, frame);
}

void setClipboardOwner(X11App& app, const std::string& text) {
  app.clipText = text;
  XSetSelectionOwner(app.display, app.clipboard, app.window, CurrentTime);
}

std::string getClipboard(X11App& app) {
  XConvertSelection(app.display, app.clipboard, app.utf8, app.prop, app.window, CurrentTime);
  XFlush(app.display);
  for (int i = 0; i < 50; ++i) {
    if (XPending(app.display) == 0) {
      fd_set set;
      FD_ZERO(&set);
      int fd = ConnectionNumber(app.display);
      FD_SET(fd, &set);
      timeval tv{0, 20000};
      select(fd + 1, &set, nullptr, nullptr, &tv);
    }
    while (XPending(app.display)) {
      XEvent event;
      XNextEvent(app.display, &event);
      if (event.type == SelectionNotify) {
        if (event.xselection.property == None) return {};
        Atom type;
        int format = 0;
        unsigned long count = 0;
        unsigned long after = 0;
        unsigned char* data = nullptr;
        XGetWindowProperty(app.display, app.window, app.prop, 0, 1 << 20, True, AnyPropertyType, &type, &format,
                           &count, &after, &data);
        std::string text;
        if (data) {
          text.assign(reinterpret_cast<char*>(data), count);
          XFree(data);
        }
        return text;
      }
    }
  }
  return {};
}

}  // namespace

int spaceviewer_x11_main(int argc, char** argv) {
  XSetLocaleModifiers("");
  std::setlocale(LC_ALL, "");
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::fprintf(stderr, "SpaceViewer: cannot open X11 display\n");
    return 1;
  }
  X11App app;
  app.display = display;
  app.screen = DefaultScreen(display);
  char executable[4096] = {};
  ssize_t executableLength = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  std::string executablePath = executableLength > 0
      ? std::string(executable, static_cast<size_t>(executableLength)) : std::string(".");
  size_t executableSlash = executablePath.find_last_of('/');
  std::string fontDirectory = (executableSlash == std::string::npos ? std::string(".") : executablePath.substr(0, executableSlash)) + "/fonts/";
  FcConfig* fontConfig = FcConfigGetCurrent();
  if (fontConfig) {
    for (const char* filename : {"Inter-Regular.ttf", "SourceHanSansSC-Regular.otf", "SourceHanSansTC-Regular.otf"}) {
      std::string path = fontDirectory + filename;
      FcConfigAppFontAddFile(fontConfig, reinterpret_cast<const FcChar8*>(path.c_str()));
    }
    FcConfigBuildFonts(fontConfig);
  }
  app.dpi = readDpi(display);
  app.colormap = DefaultColormap(display, app.screen);
  int width = std::min(1100 * app.dpi / 96, DisplayWidth(display, app.screen) * 9 / 10);
  int height = std::min(740 * app.dpi / 96, DisplayHeight(display, app.screen) * 9 / 10);
  app.width = width;
  app.height = height;
  app.window = XCreateSimpleWindow(display, RootWindow(display, app.screen), 0, 0, width, height, 0,
                                   BlackPixel(display, app.screen), WhitePixel(display, app.screen));
  app.gc = XCreateGC(display, app.window, 0, nullptr);
  app.wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  app.clipboard = XInternAtom(display, "CLIPBOARD", False);
  app.utf8 = XInternAtom(display, "UTF8_STRING", False);
  app.targets = XInternAtom(display, "TARGETS", False);
  app.prop = XInternAtom(display, "SPACEVIEWER_CLIP", False);
  XSetWMProtocols(display, app.window, &app.wmDelete, 1);
  XStoreName(display, app.window, "SpaceViewer");
  XSelectInput(display, app.window,
               ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | FocusChangeMask);
  app.xim = XOpenIM(display, nullptr, nullptr, nullptr);
  if (app.xim) app.xic = XCreateIC(app.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow,
                                   app.window, XNFocusWindow, app.window, nullptr);
  app.draw = XftDrawCreate(display, app.window, DefaultVisual(display, app.screen), app.colormap);

  Host host;
  linuxFillHost(host);
  host.setClipboard = [&app](const std::string& text) { setClipboardOwner(app, text); };
  host.getClipboard = [&app]() { return getClipboard(app); };
  host.setTitle = [&app](const std::string& title) { XStoreName(app.display, app.window, title.c_str()); };

  app.app.setWake([&app]() {
    XEvent event{};
    event.type = Expose;
    event.xexpose.window = app.window;
    XSendEvent(app.display, app.window, False, ExposureMask, &event);
    XFlush(app.display);
  });

  auto paint = [&]() {
    app.input.windowW = app.width;
    app.input.windowH = app.height;
    app.input.dpi = static_cast<float>(app.dpi);
    app.input.chromeDpi = static_cast<float>(readDpi(display, false));
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    app.input.timeSec = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
    Frame frame;
    Input uiInput = app.input;
    const int windowHeight = uiInput.windowH;
    linuxMenuPrepare(app.menu, uiInput, false);
    if (app.menu.closeRequested) app.running = false;
    if (app.menu.rootScanRequested) {
      app.menu.rootScanRequested = false;
      if (!linuxLaunchRootScan()) std::fprintf(stderr, "SpaceViewer: cannot start pkexec root scan\n");
      else app.running = false;
    }
    if (app.menu.contextCommand != MenuCommand{}) {
      app.app.applyMenu(app.menu.contextCommand, host);
      app.menu.contextCommand = MenuCommand{};
    }
    app.app.update(uiInput, host, frame);
    MenuRequest context;
    if (app.app.takeMenu(context))
      linuxMenuShowContext(app.menu, context, uiInput.windowW, windowHeight, uiInput.controlDpi(), false);
    linuxMenuPresent(app.menu, frame, windowHeight, uiInput.controlDpi(), false);
    present(app, frame);
    clearEdges(app.input);
  };

  XMapWindow(display, app.window);
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != '-') {
      app.app.scanPath(argv[i]);
      break;
    }
  }

  while (app.running) {
    if (XPending(display) == 0) {
      fd_set set;
      FD_ZERO(&set);
      int fd = ConnectionNumber(display);
      FD_SET(fd, &set);
      timeval wait{0, app.app.animating() ? 16000 : 200000};
      select(fd + 1, &set, nullptr, nullptr, &wait);
      if (XPending(display) == 0) {
        if (app.app.animating()) paint();
        continue;
      }
    }
    XEvent event;
    XNextEvent(display, &event);
    if (app.xic && XFilterEvent(&event, app.window)) continue;
    if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == app.wmDelete) {
      app.running = false;
    } else if (event.type == ConfigureNotify) {
      app.width = event.xconfigure.width;
      app.height = event.xconfigure.height;
      paint();
    } else if (event.type == Expose && event.xexpose.count == 0) {
      paint();
    } else if (event.type == MotionNotify) {
      app.input.mouseX = event.xmotion.x;
      app.input.mouseY = event.xmotion.y;
      paint();
    } else if (event.type == ButtonPress && event.xbutton.button == 1) {
      app.input.mouseX = event.xbutton.x;
      app.input.mouseY = event.xbutton.y;
      app.input.leftPressed = true;
      paint();
    } else if (event.type == ButtonPress && event.xbutton.button == 3) {
      app.input.mouseX = event.xbutton.x;
      app.input.mouseY = event.xbutton.y;
      app.input.rightPressed = true;
      paint();
    } else if (event.type == KeyPress) {
      KeySym sym = 0;
      char buffer[64];
      Status status = 0;
      int length = 0;
      if (app.xic) {
        length = Xutf8LookupString(app.xic, &event.xkey, buffer, sizeof(buffer) - 1, &sym, &status);
      } else {
        length = XLookupString(&event.xkey, buffer, sizeof(buffer) - 1, &sym, nullptr);
      }
      bool ctrl = (event.xkey.state & ControlMask) != 0;
      bool alt = (event.xkey.state & Mod1Mask) != 0;
      app.input.modCtrl = ctrl;
      app.input.modAlt = alt;
      if (ctrl && (sym == XK_c || sym == XK_C)) app.input.copy = true;
      else if (ctrl && (sym == XK_v || sym == XK_V)) app.input.paste = true;
      else if (ctrl && (sym == XK_a || sym == XK_A)) app.input.selectAll = true;
      else if (ctrl && (sym == XK_o || sym == XK_O)) app.input.openShortcut = true;
      else if (sym == XK_F5) app.input.keyF5 = true;
      else if (sym == XK_BackSpace) app.input.keyBackspace = true;
      else if (sym == XK_Delete) app.input.keyDelete = true;
      else if (sym == XK_Return || sym == XK_KP_Enter) app.input.keyEnter = true;
      else if (sym == XK_Tab) app.input.keyTab = true;
      else if (sym == XK_Escape) app.input.keyEsc = true;
      else if (sym == XK_Left) app.input.keyLeft = alt ? (app.input.keyBackNav = true, false) : true;
      else if (sym == XK_Right) app.input.keyRight = alt ? (app.input.keyForwardNav = true, false) : true;
      else if (sym == XK_Up && alt) app.input.keyUpNav = true;
      else if (sym == XK_Home) app.input.keyHome = true;
      else if (sym == XK_End) app.input.keyEnd = true;
      else if (!ctrl && length > 0 && (status == XLookupChars || status == XLookupBoth || app.xic == nullptr)) {
        buffer[length] = 0;
        app.input.typed += buffer;
      }
      paint();
    } else if (event.type == SelectionRequest) {
      XSelectionRequestEvent* request = &event.xselectionrequest;
      XSelectionEvent reply{};
      reply.type = SelectionNotify;
      reply.display = request->display;
      reply.requestor = request->requestor;
      reply.selection = request->selection;
      reply.target = request->target;
      reply.time = request->time;
      reply.property = request->property;
      if (request->target == app.targets) {
        Atom atoms[3] = {app.targets, app.utf8, XA_STRING};
        XChangeProperty(display, request->requestor, request->property, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(atoms), 3);
      } else if (request->target == app.utf8 || request->target == XA_STRING) {
        XChangeProperty(display, request->requestor, request->property, request->target, 8, PropModeReplace,
                        reinterpret_cast<unsigned char*>(const_cast<char*>(app.clipText.data())),
                        static_cast<int>(app.clipText.size()));
      } else {
        reply.property = None;
      }
      XSendEvent(display, request->requestor, False, 0, reinterpret_cast<XEvent*>(&reply));
    }
  }

  for (XftFont* font : app.fonts) {
    if (font) XftFontClose(display, font);
  }
  for (XftFont* font : app.cjkFonts) {
    if (font) XftFontClose(display, font);
  }
  if (app.draw) XftDrawDestroy(app.draw);
  if (app.xic) XDestroyIC(app.xic);
  if (app.xim) XCloseIM(app.xim);
  XFreeGC(display, app.gc);
  XDestroyWindow(display, app.window);
  XCloseDisplay(display);
  return 0;
}
