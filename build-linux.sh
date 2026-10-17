#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build/protocols
protocols=$(pkg-config --variable=pkgdatadir wayland-protocols)
wayland-scanner client-header "$protocols/stable/xdg-shell/xdg-shell.xml" build/protocols/xdg-shell-client-protocol.h
wayland-scanner private-code "$protocols/stable/xdg-shell/xdg-shell.xml" build/protocols/xdg-shell-protocol.c
wayland-scanner client-header "$protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" build/protocols/xdg-decoration-client-protocol.h
wayland-scanner private-code "$protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" build/protocols/xdg-decoration-protocol.c
c++ -std=c++17 -O2 -D_GNU_SOURCE -Isrc -Isrc/platform/linux -Ibuild/protocols -o spaceviewer \
  src/main.cpp src/util.cpp src/layout.cpp src/scan.cpp src/ui.cpp src/export.cpp src/selftest.cpp \
  src/motion.cpp src/opencl_fill.cpp \
  src/platform/linux/main.cpp src/platform/linux/host.cpp src/platform/linux/x11.cpp src/platform/linux/wayland.cpp \
  build/protocols/xdg-shell-protocol.c build/protocols/xdg-decoration-protocol.c \
  $(pkg-config --cflags --libs wayland-client xkbcommon fontconfig freetype2) \
  -lX11 -lXft -lpthread -ldl
echo "Built ./spaceviewer"
