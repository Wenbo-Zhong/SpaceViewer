#!/bin/sh
set -eu
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cp build/spaceviewer ./spaceviewer
mkdir -p fonts
cp build/fonts/* fonts/
echo "Built ./spaceviewer"
