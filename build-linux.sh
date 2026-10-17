#!/bin/sh
set -eu
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -o spaceviewer \
  src/main.cpp src/util.cpp src/layout.cpp src/scan.cpp src/ui.cpp src/export.cpp src/selftest.cpp src/motion.cpp src/opencl_fill.cpp src/platform_x11.cpp \
  -lX11 -lXft -lfontconfig -lfreetype -lpthread -ldl
echo "Built ./spaceviewer"
