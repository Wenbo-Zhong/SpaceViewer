#pragma once

#include <cstdint>
#include <string_view>

enum Category : uint8_t {
  CatOther = 0, CatDir, CatImage, CatVideo, CatAudio, CatArchive,
  CatDoc, CatCode, CatExe, CatFree
};

uint8_t categoryFromName(std::string_view name);
