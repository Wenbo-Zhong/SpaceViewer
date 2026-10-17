#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int spaceviewer_x11_main(int argc, char** argv);
int spaceviewer_wayland_main(int argc, char** argv);

int spaceviewer_main(int argc, char** argv) {
  std::signal(SIGCHLD, SIG_IGN);
  const char* backend = std::getenv("SPACEVIEWER_BACKEND");
  bool wantWayland = false;
  if (backend && std::strcmp(backend, "x11") == 0) wantWayland = false;
  else if (backend && std::strcmp(backend, "wayland") == 0) wantWayland = true;
  else {
    const char* display = std::getenv("WAYLAND_DISPLAY");
    wantWayland = display && display[0] != '\0';
  }
  if (wantWayland) {
    int code = spaceviewer_wayland_main(argc, argv);
    if (code != 2) return code;
    if (backend && std::strcmp(backend, "wayland") == 0) {
      std::fprintf(stderr, "SpaceViewer: cannot open Wayland display\n");
      return 1;
    }
    std::fprintf(stderr, "SpaceViewer: Wayland is unavailable, using X11\n");
  }
  return spaceviewer_x11_main(argc, argv);
}
