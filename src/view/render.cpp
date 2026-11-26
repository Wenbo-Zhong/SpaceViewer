#include "app/state.hpp"
#include "view/metrics.hpp"

void App::State::render(const Input& input, const Host& host, Frame& frame) {
  const ViewMetrics metrics(input);
  frame.resize(metrics.width, metrics.height, 0xf0f0f0);
  std::vector<HotRect> next;
  const int driveX = renderControls(input, host, frame, next);
  renderTreemap(input, frame, next);
  renderStatus(input, frame, next);
  renderDrivePopup(input, frame, next, driveX);
  hots.swap(next);
}
