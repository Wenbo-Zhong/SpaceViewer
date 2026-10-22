#pragma once

#include <algorithm>
#include <cmath>

// Keep physical-density fallback separate from the compositor's buffer scale:
// multiplying both would apply desktop scaling twice.
inline double linuxDensityScale(int width, int height, int widthMm, int heightMm) {
  if (width <= 0 || height <= 0 || widthMm < 100 || heightMm < 100) return 1.0;
  const double dpi = 25.4 * std::hypot(width, height) / std::hypot(widthMm, heightMm);
  if (!std::isfinite(dpi) || dpi < 72 || dpi > 384) return 1.0;
  return std::clamp(std::round(dpi / 96.0 * 4.0) / 4.0, 1.0, 4.0);
}
