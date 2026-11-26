#include "platform/apple/text_renderer.hpp"
#include "i18n/text.hpp"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

// Control the locale dependency while exercising the shipped AppKit renderer.
static zh::Language currentLanguage = zh::Language::SimplifiedChinese;
namespace zh { Language language() { return currentLanguage; } }

namespace {
using spaceviewer::apple::TextRenderer;
void expect(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
std::vector<unsigned char> render(TextRenderer& renderer, const std::vector<Frame::Text>& texts,
                                 double scale = 1) {
  const int w = 480, h = 240;
  std::vector<unsigned char> pixels(w * h * 4, 255);
  CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4, color,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  expect(ctx != nullptr, "offscreen canvas exists");
  CGContextScaleCTM(ctx, scale, scale);
  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO]];
  CGRect clip = CGContextGetClipBoundingBox(ctx);
  renderer.draw(texts, scale, h / scale);
  expect(CGRectEqualToRect(clip, CGContextGetClipBoundingBox(ctx)), "renderer preserves context clipping");
  [NSGraphicsContext restoreGraphicsState];
  CGContextFlush(ctx);
  CGContextRelease(ctx); CGColorSpaceRelease(color);
  return pixels;
}
Frame::Text label() {
  Frame::Text text;
  text.x = 40; text.y = 50; text.w = 160; text.h = 30; text.fontPx = 14;
  text.utf8 = u8"gyp 中文😀 long file name.bin";
  return text;
}
void hitAndInvalidation() {
  TextRenderer cached;
  auto text = label();
  auto first = render(cached, {text});
  expect(render(cached, {text}) == first && cached.cachedEntries() == 1, "cache hit preserves pixels");
  expect(std::any_of(first.begin(), first.end(), [](unsigned char p) { return p != 255; }), "label contains ink");
  for (int mode = 0; mode < 6; ++mode) {
    auto next = text;
    if (mode == 0) next.w = 60;
    if (mode == 1) next.center = true;
    if (mode == 2) next.rgb = 0xff0000;
    if (mode == 3) next.fontPx = 20;
    if (mode == 4) { next.x += 20; next.y += 20; next.h = 40; }
    if (mode == 5) next.utf8 = u8"different gyp 中文";
    TextRenderer fresh;
    auto pixels = render(cached, {next});
    expect(pixels == render(fresh, {next}) && pixels != first, "current geometry and style match uncached rendering");
  }
  TextRenderer fresh;
  expect(render(cached, {text}, 2) == render(fresh, {text}, 2) && cached.cachedEntries() == 1,
         "backing scale change invalidates prepared text");
  currentLanguage = zh::Language::TraditionalChinese;
  TextRenderer traditional;
  expect(render(cached, {text}, 2) == render(traditional, {text}, 2) && cached.cachedEntries() == 1,
         "locale change invalidates prepared fonts and labels");
}
void newlineAndUtf8() {
  TextRenderer renderer;
  auto text = label(); text.utf8 = u8"g\r\n中\u2028😀y";
  auto original = text.utf8;
  auto pixels = render(renderer, {text});
  auto normalized = text; normalized.utf8 = u8"g  中 😀y";
  expect(pixels == render(renderer, {normalized}) && text.utf8 == original,
         "newline display normalization preserves raw filename");
  auto invalid = text; invalid.utf8 = std::string("\xff", 1);
  expect(render(renderer, {invalid}) == std::vector<unsigned char>(480 * 240 * 4, 255),
         "invalid UTF8 does not draw or crash");
}
void boundedEviction() {
  TextRenderer cached;
  auto text = label();
  auto original = render(cached, {text});
  for (int i = 0; i < 2400; ++i) {
    text.utf8 = "unique_" + std::to_string(i);
    render(cached, {text});
    expect(cached.cachedEntries() <= TextRenderer::entryLimit &&
           cached.cachedTextCost() <= TextRenderer::textCostLimit, "cache remains bounded");
  }
  expect(render(cached, {label()}) == original, "evicted label rebuilds identical pixels");
  text.utf8.assign(250000, 'g');
  auto count = cached.cachedEntries();
  render(cached, {text});
  expect(cached.cachedEntries() == count, "oversized label is drawn without retention");
  text.utf8.assign(1024, 'g');
  for (int i = 0; i < 300; ++i) {
    text.utf8.back() = 'a' + i % 26;
    text.rgb = i;
    render(cached, {text});
  }
  expect(cached.cachedTextCost() <= TextRenderer::textCostLimit, "long labels obey text cost bound");
}
}  // namespace
int main() {
  @autoreleasepool {
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
      {"hitAndInvalidation", hitAndInvalidation}, {"newlineAndUtf8", newlineAndUtf8},
      {"boundedEviction", boundedEviction}
    };
    int failures = 0;
    for (const auto& item : cases) {
      try { item.run(); std::printf("case=%s PASS\n", item.name); }
      catch (const std::exception& error) {
        ++failures; std::fprintf(stderr, "case=%s FAIL %s\n", item.name, error.what());
      }
    }
    return failures ? 1 : 0;
  }
}
