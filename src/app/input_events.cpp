#include "app/input.hpp"

void Input::clearEdges() {
  command = -1;
  leftPressed = rightPressed = false;
  keyBackspace = keyDelete = keyEnter = keyTab = keyEsc = false;
  keyLeft = keyRight = keyHome = keyEnd = keyF5 = false;
  keyUpNav = keyBackNav = keyForwardNav = false;
  copy = paste = selectAll = openShortcut = false;
  typed.clear();
}
