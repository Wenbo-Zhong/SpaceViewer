#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang++ -std=c++17 -O2 -fobjc-arc -o SpaceViewer \
  src/main.cpp src/util.cpp src/layout.cpp src/scan.cpp src/ui.cpp src/export.cpp src/selftest.cpp src/motion.cpp src/opencl_fill.cpp src/platform_mac.mm \
  -framework Cocoa -framework CoreText -framework CoreGraphics -framework CoreServices
echo "Built ./SpaceViewer"
