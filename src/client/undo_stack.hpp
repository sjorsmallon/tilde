#pragma once

#include <functional>
#include <vector>

namespace client
{

struct undo_redo_t
{
  std::function<void()> undo;
  std::function<void()> redo;
};

class Undo_Stack
{
public:
  void push(std::function<void()> undo, std::function<void()> redo)
  {
    // When we push a new action, we invalidate the redo history
    if (cursor < stack.size())
    {
      stack.resize(cursor);
    }
    stack.push_back({std::move(undo), std::move(redo)});
    cursor++;
  }

  void undo()
  {
    if (cursor > 0)
    {
      cursor--;
      stack[cursor].undo();
    }
  }

  void redo()
  {
    if (cursor < stack.size())
    {
      stack[cursor].redo();
      cursor++;
    }
  }

  bool can_undo() const { return cursor > 0; }
  bool can_redo() const { return cursor < stack.size(); }

private:
  std::vector<undo_redo_t> stack;
  size_t cursor = 0;
};

} // namespace client
