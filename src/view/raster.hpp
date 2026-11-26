#pragma once

#include "view/frame.hpp"
#include "view/tile.hpp"

void fillRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb);
void fillGradient(Frame& frame, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void strokeRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb, int thickness);
void roundRect(Frame& frame, int x, int y, int w, int h, int radius, uint32_t color);
void drawText(Frame& frame, int x, int y, int w, int h, const std::string& text,
              uint32_t rgb, int fontPx, bool center);

// Fill all tile pixels before borders/text, matching the OpenCL compositor's order.
void fillTilePixels(Frame& frame, const std::vector<Tile>& tiles, int x, int y, int w, int h,
                    int dividerPx, bool fileGroups);
