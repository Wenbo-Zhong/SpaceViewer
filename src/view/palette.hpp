#pragma once

#include <cstdint>

uint32_t categoryColor(uint8_t category);
uint32_t shadeColor(uint32_t rgb, int delta);
uint32_t mixColor(uint32_t a, uint32_t b, int tNum, int tDen);
