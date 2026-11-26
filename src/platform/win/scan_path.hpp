#pragma once

#include <string>

namespace spaceviewer::win {
std::wstring joinWide(const std::wstring& dir, const std::wstring& name);
std::wstring extendedPath(const std::wstring& path);
bool isVolumeRoot(const std::wstring& path);
std::wstring normalizeWide(const std::wstring& input);
}  // namespace spaceviewer::win
