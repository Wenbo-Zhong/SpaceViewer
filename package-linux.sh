#!/bin/sh
set -eu
cd "$(dirname "$0")"

cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON
cmake --build build/linux-release --parallel "${JOBS:-2}"
mkdir -p out
name="SpaceViewer-ubuntu-$(uname -m)"
stage="out/$name"
mkdir -p "$stage"
cp build/linux-release/spaceviewer "$stage/spaceviewer"
cp -R build/linux-release/fonts "$stage/"
strip "$stage/spaceviewer"
chmod 755 "$stage/spaceviewer"
"$stage/spaceviewer" --self-test
tar -C out -czf "out/$name.tar.gz" "$name"
echo "Built out/$name.tar.gz"
