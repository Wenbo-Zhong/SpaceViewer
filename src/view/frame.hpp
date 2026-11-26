#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 共享绘制输出：不透明 BGRA 位图与 UTF-8 文字指令，坐标和字号使用物理像素。
// 宿主先呈现位图，再用原生文字接口绘制 texts；可跨帧复用此对象的缓冲区。
struct Frame {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> bgra;
  struct Text {
    int x = 0, y = 0, w = 0, h = 0;
    int fontPx = 14;
    uint32_t rgb = 0;
    bool center = false;
    std::string utf8;
  };
  std::vector<Text> texts;

  void resize(int w, int h, uint32_t clear);
};

inline constexpr uint32_t packRgb(uint32_t rgb) {
  return 0xff000000u | (rgb & 0x00ffffffu);
}
