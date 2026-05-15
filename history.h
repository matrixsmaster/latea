#pragma once

#include <vector>
#include <string>
#include <FL/Fl_Text_Buffer.H>

struct edit_action
{
    int pos;
    std::string removed;
    std::string inserted;
    int cursor_before;
    int cursor_after;
};

struct edit_history
{
    std::vector<edit_action> undo_stack;
    std::vector<edit_action> redo_stack;
    bool replaying;
    double last_edit_time;

    void clear();
    void record(const edit_action &action);
    bool undo(Fl_Text_Buffer* textbuf, int &cursor_pos);
    bool redo(Fl_Text_Buffer* textbuf, int &cursor_pos);
};
