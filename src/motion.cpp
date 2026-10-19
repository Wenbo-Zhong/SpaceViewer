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

RectF project(RectF rect, RectF from, RectF to) {
  return {to.x + (rect.x - from.x) * to.w / from.w,
          to.y + (rect.y - from.y) * to.h / from.h,
          rect.w * to.w / from.w, rect.h * to.h / from.h};
}

RectF clip(RectF rect, RectF viewport) {
  double x = std::max(rect.x, viewport.x);
  double y = std::max(rect.y, viewport.y);
  double right = std::min(rect.x + rect.w, viewport.x + viewport.w);
  double bottom = std::min(rect.y + rect.h, viewport.y + viewport.h);
  return {x, y, std::max(0.0, right - x), std::max(0.0, bottom - y)};
}

}  // namespace

uint64_t TileMotion::keyOf(const Tile& tile) {
  if (tile.index != kNone) return tile.index;
  return (1ull << 63) | (static_cast<uint64_t>(tile.parent) << 32) | tile.mergeCount;
}

void TileMotion::setTarget(const std::vector<Tile>& target, double now, uint32_t view, RectF viewport, bool immediate) {
  if (immediate) {
    target_ = target;
    from_.clear();
    outgoing_.clear();
    view_ = view;
    viewport_ = viewport;
    navigation_ = false;
    zoomOut_ = false;
    active_ = false;
    return;
  }
  const bool changedView = view_ != kNone && view != kNone && view_ != view;
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
  if (sameTarget && !changedView) return;
  std::vector<Tile> current = sample(now);
  navigation_ = false;
  zoomOut_ = false;
  outgoing_.clear();
  viewport_ = viewport;
  if (changedView && viewport.w > 1 && viewport.h > 1) {
    for (const Tile& tile : current) {
      if (!tile.exiting && tile.index == view && tile.outer.w > 1 && tile.outer.h > 1) {
        cameraFrom_ = tile.outer;
        cameraTo_ = viewport;
        navigation_ = true;
        break;
      }
    }
    if (!navigation_) {
      for (const Tile& tile : target) {
        if (tile.index == view_ && tile.outer.w > 1 && tile.outer.h > 1) {
          cameraFrom_ = viewport;
          cameraTo_ = tile.outer;
          navigation_ = true;
          zoomOut_ = true;
          break;
        }
      }
    }
  }
  view_ = view;
  target_ = target;
  std::unordered_map<uint64_t, RectF> outer;
  std::unordered_map<uint64_t, RectF> title;
  outer.reserve(current.size() * 2 + 1);
  for (const Tile& tile : current) {
    if (!tile.exiting) {
      outer[keyOf(tile)] = tile.outer;
      title[keyOf(tile)] = tile.title;
    }
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
      if (navigation_) {
        RectF origin = project(tile.outer, cameraTo_, cameraFrom_);
        keyed.tile.outer = origin;
        keyed.tile.title = project(tile.title, cameraTo_, cameraFrom_);
        keyed.tile.labelOpacity = 0;
        from_.push_back(keyed);
        continue;
      }
      keyed.tile.outer = tile.outer;
      keyed.tile.title = tile.title;
    }
    from_.push_back(keyed);
  }
  if (navigation_) {
    for (const Tile& tile : current) {
      if (tile.exiting) continue;
      bool retained = false;
      for (const Tile& next : target) {
        if (keyOf(next) == keyOf(tile)) { retained = true; break; }
      }
      if (!retained) outgoing_.push_back(tile);
    }
  }
  start_ = now;
  duration_ = navigation_ ? 0.16 : 0.26;
  active_ = true;
}

std::vector<Tile> TileMotion::sample(double now) const {
  if (!active_ || from_.empty()) return target_;
  double progress = std::max(0.0, std::min(1.0, (now - start_) / duration_));
  double t = navigation_ ? 1.0 - std::pow(1.0 - progress, 3.0) : ease(progress);
  if (t >= 1.0) return target_;
  std::unordered_map<uint64_t, const Keyed*> from;
  from.reserve(from_.size() * 2 + 1);
  for (const Keyed& keyed : from_) from[keyed.key] = &keyed;
  std::vector<Tile> out;
  out.reserve(target_.size() + outgoing_.size());
  auto appendOutgoing = [&]() {
    for (const Tile& old : outgoing_) {
      Tile drawn = old;
      drawn.outer = clip(lerpRect(old.outer, project(old.outer, cameraFrom_, cameraTo_), t), viewport_);
      drawn.title = clip(lerpRect(old.title, project(old.title, cameraFrom_, cameraTo_), t), viewport_);
      drawn.labelOpacity = old.labelOpacity * (1.0 - t);
      drawn.exiting = true;
      if (drawn.outer.w > 0 && drawn.outer.h > 0) out.push_back(drawn);
    }
  };
  if (navigation_ && !zoomOut_) appendOutgoing();
  for (const Tile& tile : target_) {
    Tile drawn = tile;
    auto found = from.find(keyOf(tile));
    if (found != from.end()) {
      drawn.outer = lerpRect(found->second->tile.outer, tile.outer, t);
      drawn.title = lerpRect(found->second->tile.title, tile.title, t);
      drawn.labelOpacity = found->second->tile.labelOpacity +
                           (1.0 - found->second->tile.labelOpacity) * t;
    }
    if (navigation_) {
      drawn.outer = clip(drawn.outer, viewport_);
      drawn.title = clip(drawn.title, viewport_);
    }
    out.push_back(drawn);
  }
  if (navigation_ && zoomOut_) appendOutgoing();
  return out;
}

bool TileMotion::running(double now) const {
  return active_ && (now - start_) < duration_;
}
