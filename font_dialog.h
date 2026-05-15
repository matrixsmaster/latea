#pragma once

#include <string>
#include <vector>
#include <FL/Fl.H>
#include <FL/Enumerations.H>

struct font_dialog
{
    std::vector<Fl_Font> fonts;
    std::vector<std::vector<int> > sizes;
    std::string selected_name;
    int selected_size;
    Fl_Color selected_text_color;
    Fl_Color selected_ghost_color;
    Fl_Color selected_bg_color;
    Fl_Color selected_selection_color;
    Fl_Color selected_linenumber_bg_color;
    Fl_Color selected_linenumber_fg_color;

    font_dialog();

    void open();
    void refresh_preview();
    void font_browser_changed();
    void font_size_browser_changed();
    void choose_text_color();
    void choose_ghost_color();
    void choose_background_color();
    void choose_selection_color();
    void choose_linenumber_bg_color();
    void choose_linenumber_fg_color();
    void accept();
};

extern font_dialog* g_font_dlg;
