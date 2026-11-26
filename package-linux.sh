#!/bin/sh
set -eu
cd "$(dirname "$0")"

if [ "$(uname -s)" != Linux ]; then
  echo "AppImage packaging requires Linux." >&2
  exit 1
fi

case "$(uname -m)" in
  x86_64)
    arch=x86_64
    tool_sha=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    ;;
  aarch64|arm64)
    arch=aarch64
    tool_sha=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    ;;
  *)
    echo "Unsupported AppImage architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

repo_dir=$(pwd)
build_dir="$repo_dir/build/linux-release"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON
cmake --build "$build_dir" --parallel "${JOBS:-2}"
ctest --test-dir "$build_dir" --output-on-failure
desktop-file-validate src/platform/linux/spaceviewer.desktop

# Pin the complete linuxdeploy AppImage, including its output plugin.
tool_version=1-alpha-20251107-1
tool_dir="$build_dir/appimage-tools"
linuxdeploy="$tool_dir/linuxdeploy-$arch.AppImage"
mkdir -p "$tool_dir"
if [ ! -f "$linuxdeploy" ] ||
   ! printf '%s  %s\n' "$tool_sha" "$linuxdeploy" | sha256sum --check --status; then
  curl --fail --location --retry 3 \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$tool_version/linuxdeploy-$arch.AppImage" \
    --output "$linuxdeploy.download"
  printf '%s  %s\n' "$tool_sha" "$linuxdeploy.download" | sha256sum --check
  mv "$linuxdeploy.download" "$linuxdeploy"
fi
chmod 755 "$linuxdeploy"

stage_dir=$(mktemp -d "$build_dir/appimage.XXXXXX")
trap 'rm -rf "$stage_dir"' EXIT HUP INT TERM
appdir="$stage_dir/SpaceViewer.AppDir"
mkdir -p "$appdir/usr/bin/fonts" "$repo_dir/out"
for font in Inter-Regular.ttf SourceHanSansSC-Regular.otf SourceHanSansTC-Regular.otf \
            Inter-LICENSE.txt SourceHanSans-LICENSE.txt; do
  test -s "$build_dir/fonts/$font"
  cp "$build_dir/fonts/$font" "$appdir/usr/bin/fonts/"
done

output="$repo_dir/out/SpaceViewer-$arch.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" LDAI_OUTPUT="$output" \
  "$linuxdeploy" --appdir "$appdir" \
    --executable "$build_dir/spaceviewer" \
    --desktop-file "$repo_dir/src/platform/linux/spaceviewer.desktop" \
    --icon-file "$repo_dir/assets/icons/spaceviewer.png" \
    --output appimage
test -s "$output"
chmod 755 "$output"

# Test the published image, including the runtime and AppRun library paths,
# without requiring a FUSE mount on the build runner.
(
  cd "$stage_dir"
  APPIMAGE_EXTRACT_AND_RUN=1 "$output" --self-test
  "$output" --appimage-extract >/dev/null
)
for font in Inter-Regular.ttf SourceHanSansSC-Regular.otf SourceHanSansTC-Regular.otf \
            Inter-LICENSE.txt SourceHanSans-LICENSE.txt; do
  cmp "$build_dir/fonts/$font" "$stage_dir/squashfs-root/usr/bin/fonts/$font"
done
test -s "$stage_dir/squashfs-root/spaceviewer.desktop"
test -s "$stage_dir/squashfs-root/spaceviewer.png"
echo "Built $output"
