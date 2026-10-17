#pragma once

#include "types.hpp"

#include <vector>

class TileMotion {
 public:
  void setTarget(const std::vector<Tile>& target, double now);
  std::vector<Tile> sample(double now) const;
  bool running(double now) const;

 private:
  struct Keyed {
    uint64_t key = 0;
    Tile tile;
  };
  static uint64_t keyOf(const Tile& tile);
  std::vector<Keyed> from_;
  std::vector<Tile> target_;
  double start_ = 0;
  double duration_ = 0.26;
  bool active_ = false;
};
