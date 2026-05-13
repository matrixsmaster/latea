#ifndef LATEA_EDITOR_H
#define LATEA_EDITOR_H

#include <string>
#include <vector>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Enumerations.H>
#include "prefs.h"
#include "history.h"
#include "autocomplete.h"

class LateaUI;

struct suggestion_state
{
    std::string text;
    int anchor_pos;
    int request_id;
    int visible;

    void clear();
};

class latea_editor : public Fl_Text_Editor
{
public:
    latea_editor(int x, int y, int w, int h, const char *label = 0);

    void draw();
    int handle(int event);
};

struct editor_window
{
    LateaUI *ui;
    latea_editor *editor;
    Fl_Text_Buffer *textbuf;
    app_prefs prefs;
    edit_history history;
    autocomplete_engine autocomplete;
    suggestion_state suggestion;
    std::string filename;
    int changed;
    int suppress_history;
    int last_cursor_pos;
    Fl_Font current_font;
    Fl_Fontsize current_size;
    std::vector<Fl_Font> font_dialog_fonts;
    std::vector<std::vector<int> > font_dialog_sizes;
    int font_dialog_accepted;
    std::string font_dialog_selected_name;
    int font_dialog_selected_size;
    Fl_Color font_dialog_selected_color;

    editor_window();
    ~editor_window();

    void show(int argc, char **argv);
    void new_document();
    bool open_file(const char *path);
    bool save_file();
    bool save_file_as();
    bool save_if_needed();
    void update_title();
    void update_status(const char *text);
    void update_ai_status(const char *text);
    void show_find_dialog();
    void show_preferences_dialog();
    void choose_font();
    void font_browser_changed();
    void font_size_browser_changed();
    void font_size_input_changed();
    void choose_font_color();
    void accept_font_dialog();
    void choose_background_color();
    void browse_dictionary_path();
    void find_next();
    void replace_next();
    void replace_all();
    void apply_preferences();
    void apply_view_preferences();
    void set_autocomplete_mode(int mode);
    void set_word_wrap(int enabled);
    void set_line_numbers(int enabled);
    void set_suggestion(const std::string &text, int anchor_pos, int request_id);
    void clear_suggestion();
    void accept_suggestion_full();
    void accept_suggestion_word();
    void record_buffer_change(int pos, int inserted, int deleted, const char *deleted_text);
    void set_changed(int value);
    void sync_ui_to_prefs();
    void sync_prefs_from_ui();
    void close_main_window();

    static void text_modified_cb(int pos, int nInserted, int nDeleted,
        int nRestyled, const char *deletedText, void *cbArg);
};

extern editor_window* g_wnd;

#endif
