#include "common.h"
#include "history.h"

static int can_coalesce(const edit_action &prev, const edit_action &next, double last_edit_time)
{
    double now = now_seconds();
    if (now - last_edit_time > 1.0) return 0;
    if (!prev.removed.empty() || !next.removed.empty()) {
        if (prev.inserted.empty() && next.inserted.empty() && !prev.removed.empty() && !next.removed.empty()) {
            return next.pos + (int)next.removed.size() == prev.pos || next.pos == prev.pos;
        }
        return 0;
    }
    return prev.pos + (int)prev.inserted.size() == next.pos;
}

void edit_history::clear()
{
    undo_stack.clear();
    redo_stack.clear();
    replaying = false;
    last_edit_time = 0.0;
}

void edit_history::record(const edit_action &action)
{
    if (replaying) return;

    if (!undo_stack.empty() && can_coalesce(undo_stack.back(), action, last_edit_time)) {
        edit_action &prev = undo_stack.back();
        if (!prev.inserted.empty() && !action.inserted.empty()) {
            prev.inserted += action.inserted;
            prev.cursor_after = action.cursor_after;
        } else if (!prev.removed.empty() && !action.removed.empty()) {
            if (action.pos == prev.pos) prev.removed += action.removed;
            else {
                prev.removed = action.removed + prev.removed;
                prev.pos = action.pos;
            }
            prev.cursor_after = action.cursor_after;
        } else {
            undo_stack.push_back(action);
        }
    } else {
        undo_stack.push_back(action);
    }

    redo_stack.clear();
    last_edit_time = now_seconds();
}

bool edit_history::undo(Fl_Text_Buffer *textbuf, int &cursor_pos)
{
    if (undo_stack.empty()) return false;
    replaying = true;
    edit_action action = undo_stack.back();
    undo_stack.pop_back();
    textbuf->replace(action.pos, action.pos + (int)action.inserted.size(), action.removed.c_str());
    cursor_pos = action.cursor_before;
    redo_stack.push_back(action);
    replaying = false;
    return true;
}

bool edit_history::redo(Fl_Text_Buffer *textbuf, int &cursor_pos)
{
    if (redo_stack.empty()) return false;
    replaying = true;
    edit_action action = redo_stack.back();
    redo_stack.pop_back();
    textbuf->replace(action.pos, action.pos + (int)action.removed.size(), action.inserted.c_str());
    cursor_pos = action.cursor_after;
    undo_stack.push_back(action);
    replaying = false;
    return true;
}

bool edit_history::can_undo() const
{
    return !undo_stack.empty();
}

bool edit_history::can_redo() const
{
    return !redo_stack.empty();
}
