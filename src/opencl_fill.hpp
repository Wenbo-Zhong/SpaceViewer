#pragma once

#include "types.hpp"

#include <vector>

// Fills tile rectangles, title bars and the divider under each title.
// Returns false when OpenCL is unavailable; the caller draws on the CPU.
bool openclFillTiles(Frame& frame, const std::vector<Tile>& tiles, int originX, int originY, int mapW, int mapH,
                     int dividerPx);
