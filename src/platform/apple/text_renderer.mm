#include "platform/apple/text_renderer.hpp"
#include "i18n/text.hpp"
#include "i18n/unicode.hpp"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>

namespace spaceviewer::apple {

struct TextRenderer::Impl {
  struct Fonts {
    NSFont* latin = nil;
    NSFont* cjk = nil;
  };
  struct Entry {
    std::string key;
    NSAttributedString* text = nil;
    CGFloat lineHeight = 0;
    size_t cost = 0;
  };
  double scale = 0;
  zh::Language language = zh::Language::SimplifiedChinese;
  NSParagraphStyle* left = nil;
  NSParagraphStyle* center = nil;
  std::unordered_map<int, Fonts> fonts;
  std::list<Entry> entries;
  std::unordered_map<std::string, std::list<Entry>::iterator> index;
  size_t cost = 0;

  Impl() {
    NSMutableParagraphStyle* style = [NSMutableParagraphStyle new];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    style.alignment = NSTextAlignmentLeft;
    left = [style copy];
    style.alignment = NSTextAlignmentCenter;
    center = [style copy];
  }

  void configure(double nextScale) {
    auto nextLanguage = zh::language();
    if (scale == nextScale && language == nextLanguage) return;
    entries.clear(); index.clear(); fonts.clear(); cost = 0;
    scale = nextScale; language = nextLanguage;
  }

  Fonts fontPair(int pixels) {
    auto found = fonts.find(pixels);
    if (found != fonts.end()) return found->second;
    CGFloat pointSize = std::max(8.0, pixels / scale);
    Fonts pair;
    pair.latin = [NSFont fontWithName:@"Inter" size:pointSize];
    if (!pair.latin) pair.latin = [NSFont systemFontOfSize:pointSize];
    NSString* name = language == zh::Language::TraditionalChinese ? @"Source Han Sans TC"
                                                                 : @"Source Han Sans SC";
    pair.cjk = [NSFont fontWithName:name size:pointSize];
    if (fonts.size() >= 32) fonts.clear();
    fonts.emplace(pixels, pair);
    return pair;
  }

  Entry prepare(const Frame::Text& text, std::string key) {
    Entry entry;
    NSString* string = [NSString stringWithUTF8String:text.utf8.c_str()];
    if (!string) return entry;
    NSCharacterSet* newlines = [NSCharacterSet newlineCharacterSet];
    if ([string rangeOfCharacterFromSet:newlines].location != NSNotFound)
      string = [[string componentsSeparatedByCharactersInSet:newlines] componentsJoinedByString:@" "];
    Fonts pair = fontPair(text.fontPx);
    NSColor* color = [NSColor colorWithCalibratedRed:((text.rgb >> 16) & 255) / 255.0
                                             green:((text.rgb >> 8) & 255) / 255.0
                                              blue:(text.rgb & 255) / 255.0 alpha:1];
    NSMutableAttributedString* attributed = [[NSMutableAttributedString alloc] initWithString:string attributes:@{
      NSFontAttributeName : pair.latin,
      NSForegroundColorAttributeName : color,
      NSParagraphStyleAttributeName : text.center ? center : left
    }];
    if (pair.cjk) {
      for (NSUInteger i = 0; i < string.length;) {
        NSRange range = [string rangeOfComposedCharacterSequenceAtIndex:i];
        uint32_t cp = [string characterAtIndex:i];
        if (CFStringIsSurrogateHighCharacter(static_cast<UniChar>(cp)) && i + 1 < string.length)
          cp = CFStringGetLongCharacterForSurrogatePair(static_cast<UniChar>(cp), [string characterAtIndex:i + 1]);
        if (isChinese(cp)) [attributed addAttribute:NSFontAttributeName value:pair.cjk range:range];
        i = NSMaxRange(range);
      }
    }
    entry.key = std::move(key);
    entry.text = [attributed copy];
    // Preserve AppKit's complete line box, including descenders and fallback fonts.
    entry.lineHeight = std::ceil([attributed size].height * scale) / scale;
    // Bound retained text and run overhead as well as the number of labels.
    entry.cost = entry.key.size() * 2 + string.length * 8 + 256;
    return entry;
  }

  const Entry& lookup(const Frame::Text& text, Entry& scratch) {
    // Geometry stays outside the key: AppKit applies truncation at the current width.
    std::string key = std::to_string(text.fontPx) + "/" + std::to_string(text.rgb) + "/" +
                      (text.center ? "1/" : "0/") + text.utf8;
    auto found = index.find(key);
    if (found != index.end()) {
      entries.splice(entries.begin(), entries, found->second);
      return *found->second;
    }
    scratch = prepare(text, std::move(key));
    if (!scratch.text || scratch.cost > TextRenderer::textCostLimit) return scratch;
    while (!entries.empty() && (entries.size() >= TextRenderer::entryLimit ||
           cost + scratch.cost > TextRenderer::textCostLimit)) {
      cost -= entries.back().cost;
      index.erase(entries.back().key);
      entries.pop_back();
    }
    entries.push_front(std::move(scratch));
    index.emplace(entries.front().key, entries.begin());
    cost += entries.front().cost;
    return entries.front();
  }
};

TextRenderer::TextRenderer() : impl_(new Impl) {}
TextRenderer::~TextRenderer() = default;

void TextRenderer::draw(const std::vector<Frame::Text>& texts, double scale, double canvasHeight) {
  impl_->configure(scale);
  for (const auto& text : texts) {
    Impl::Entry scratch;
    const auto& entry = impl_->lookup(text, scratch);
    if (!entry.text) continue;
    CGFloat x = text.x / scale;
    CGFloat y = canvasHeight - (text.y + text.h) / scale;
    CGFloat centeredY = y + (text.h / scale - entry.lineHeight) * 0.5;
    [entry.text drawInRect:NSMakeRect(x, centeredY, text.w / scale, entry.lineHeight)];
  }
}

size_t TextRenderer::cachedEntries() const { return impl_->entries.size(); }
size_t TextRenderer::cachedTextCost() const { return impl_->cost; }

}  // namespace spaceviewer::apple
