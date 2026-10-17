#include "motion.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

double ease(double t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  return t * t * (3.0 - 2.0 * t);
}

RectF lerpRect(const RectF& a, const RectF& b, double t) {
  RectF out;
  out.x = a.x + (b.x - a.x) * t;
  out.y = a.y + (b.y - a.y) * t;
  out.w = a.w + (b.w - a.w) * t;
  out.h = a.h + (b.h - a.h) * t;
  return out;
}

bool near(const RectF& a, const RectF& b) {
  return std::fabs(a.x - b.x) < 0.4 && std::fabs(a.y - b.y) < 0.4 && std::fabs(a.w - b.w) < 0.4 &&
         std::fabs(a.h - b.h) < 0.4;
}

RectF collapsed(const RectF& around) {
  RectF out;
  out.x = around.x + around.w * 0.5;
  out.y = around.y + around.h * 0.5;
  return out;
}

}  // namespace

uint64_t TileMotion::keyOf(const Tile& tile) {
  if (tile.index != kNone) return tile.index;
  return (1ull << 63) | (static_cast<uint64_t>(tile.parent) << 32) | tile.mergeCount;
}

void TileMotion::setTarget(const std::vector<Tile>& target, double now) {
  bool sameTarget = target_.size() == target.size();
  if (sameTarget) {
    for (size_t i = 0; i < target.size(); ++i) {
      if (keyOf(target_[i]) != keyOf(target[i]) || !near(target_[i].outer, target[i].outer) ||
          !near(target_[i].title, target[i].title)) {
        sameTarget = false;
        break;
      }
    }
  }
  if (sameTarget) return;
  std::vector<Tile> current = sample(now);
  target_ = target;
  std::unordered_map<uint64_t, RectF> outer;
  std::unordered_map<uint64_t, RectF> title;
  outer.reserve(current.size() * 2 + 1);
  for (const Tile& tile : current) {
    outer[keyOf(tile)] = tile.outer;
    title[keyOf(tile)] = tile.title;
  }
  std::unordered_map<uint32_t, RectF> byIndex;
  for (const Tile& tile : current) {
    if (tile.index != kNone) byIndex[tile.index] = tile.outer;
  }
  from_.clear();
  from_.reserve(target.size());
  for (const Tile& tile : target) {
    Keyed keyed;
    keyed.key = keyOf(tile);
    keyed.tile = tile;
    auto found = outer.find(keyed.key);
    if (found != outer.end()) {
      keyed.tile.outer = found->second;
      keyed.tile.title = title[keyed.key];
    } else {
      RectF origin = collapsed(tile.outer);
      auto parent = byIndex.find(tile.parent);
      if (parent != byIndex.end()) origin = collapsed(parent->second);
      keyed.tile.outer = origin;
      keyed.tile.title = origin;
      keyed.tile.title.h = 0;
    }
    from_.push_back(keyed);
  }
  start_ = now;
  duration_ = 0.26;
  active_ = true;
}

std::vector<Tile> TileMotion::sample(double now) const {
  if (!active_ || from_.empty()) return target_;
  double t = ease((now - start_) / duration_);
  if (t >= 1.0) return target_;
  std::unordered_map<uint64_t, const Keyed*> from;
  from.reserve(from_.size() * 2 + 1);
  for (const Keyed& keyed : from_) from[keyed.key] = &keyed;
  std::vector<Tile> out;
  out.reserve(target_.size());
  for (const Tile& tile : target_) {
    Tile drawn = tile;
    auto found = from.find(keyOf(tile));
    if (found != from.end()) {
      drawn.outer = lerpRect(found->second->tile.outer, tile.outer, t);
      drawn.title = lerpRect(found->second->tile.title, tile.title, t);
    }
    out.push_back(drawn);
  }
  return out;
}

bool TileMotion::running(double now) const {
  return active_ && (now - start_) < duration_;
}
