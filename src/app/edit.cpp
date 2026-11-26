#include "app/edit.hpp"
#include "i18n/unicode.hpp"

void insertText(Edit& edit, const std::string& text) {
  if (edit.selectAll) {
    edit.text.clear();
    edit.cursor = 0;
    edit.selectAll = false;
  }
  if (edit.cursor > edit.text.size()) edit.cursor = edit.text.size();
  edit.text.insert(edit.cursor, text);
  edit.cursor += text.size();
}

void editKey(Edit& edit, const Input& input) {
  if (input.selectAll) {
    edit.selectAll = true;
    return;
  }
  if (input.copy || input.paste) return;
  if (input.keyHome) {
    edit.cursor = 0;
    edit.selectAll = false;
    return;
  }
  if (input.keyEnd) {
    edit.cursor = edit.text.size();
    edit.selectAll = false;
    return;
  }
  if (input.keyLeft) {
    edit.cursor = utf8Prev(edit.text, edit.cursor);
    edit.selectAll = false;
    return;
  }
  if (input.keyRight) {
    edit.cursor = utf8Next(edit.text, edit.cursor);
    edit.selectAll = false;
    return;
  }
  if (input.keyBackspace || input.keyDelete) {
    if (edit.selectAll) {
      edit.text.clear();
      edit.cursor = 0;
      edit.selectAll = false;
    } else if (input.keyBackspace) {
      size_t prev = utf8Prev(edit.text, edit.cursor);
      if (prev != edit.cursor) edit.text.erase(prev, edit.cursor - prev);
      edit.cursor = prev;
    } else {
      size_t next = utf8Next(edit.text, edit.cursor);
      if (next != edit.cursor) edit.text.erase(edit.cursor, next - edit.cursor);
    }
    return;
  }
  if (!input.typed.empty()) insertText(edit, input.typed);
}
