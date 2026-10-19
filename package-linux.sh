#!/bin/sh
set -eu
cd "$(dirname "$0")"

cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON
cmake --build build/linux-release --parallel "${JOBS:-2}"
mkdir -p out
output="out/SpaceViewer-ubuntu-$(uname -m)"
cp build/linux-release/spaceviewer "$output"
strip "$output"
chmod 755 "$output"
"$output" --self-test
echo "Built $output"
