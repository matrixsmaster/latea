#pragma once

#include <string>
#include <vector>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Enumerations.H>
#include <FL/Fl_PNG_Image.H>
#include "prefs.h"
#include "prefs_dlg.h"
#include "history.h"
#include "autocomp.h"

#define EDITOR_SCROLL_ATTEMPTS 2

class LateaUI;
struct font_dialog;

struct suggestion_state
{
    std::string text;
    std::vector<std::string> vars;
    int anchor_pos;
    int request_id;
    int var_idx;
    bool visible;

    void clear();
};

class latea_editor : public Fl_Text_Editor
{
public:
    latea_editor(int x, int y, int w, int h, const char* label = 0);

    void draw();
    int handle(int event);

private:
    int scrolled = 0;
};

struct latea
{
    LateaUI* ui;
    latea_editor* editor;
    std::string filename;
    bool changed;
    Fl_Text_Buffer* textbuf;
    app_prefs prefs;
    edit_history history;
    autocomp* cmpt;
    autocomp_dict cmpt_dict;
    autocomp_file cmpt_file;
    autocomp_markov cmpt_mkv;
    autocomp_lan_ai cmpt_ai;
    autocomp_emb_ai cmpt_emb_ai;
    suggestion_state suggest;
    int suppress_history, suppress_autocomp;
    Fl_Font current_font;
    Fl_Fontsize current_size;
    int line_count_cache, line_number_digits;
    bool find_selected;
    std::string ai_status_text;
    int ai_used_toks, ai_used_ctx;
    int text_rev;
    Fl_PNG_Image* app_icon;
    Fl_Image* about_img;
    std::string file_text;

    latea();

    void new_document();
    bool open_file(const char* path);
    bool save_file();
    bool save_file_as();
    bool save_if_needed();
    void update_title();
    void update_status(const char* text);
    void update_ai_status(const char* text);
    void show_find_dialog(bool replace_mode);
    void choose_font();
    void browse_dictionary_path();
    void browse_model_path();
    void browse_llama_path();
    void find_next();
    void replace_next();
    void replace_all();
    void apply_preferences();
    void apply_view_preferences();
    void set_autocomp_mode(int mode);
    void select_autocomp();
    void set_word_wrap(bool enabled);
    void set_line_numbers(bool enabled);
    void show_stats();
    void reset_autocomp();
    void set_suggestion(const std::string &text, int anchor_pos, int request_id = 0);
    void set_suggestion_list(std::vector<std::string> vars, int var_idx, int anchor_pos, int request_id = 0);
    void clear_suggestion();
    void prev_suggestion();
    void next_suggestion();
    void accept_suggestion_full();
    void accept_suggestion_word();
    void record_buffer_change(int pos, int inserted, int deleted, const char* deleted_text);
    void set_changed(bool value);
    void update_caret_pos();
    void update_ai_usage(int used, int ctx);
    void refresh_ai_status();
    void close_main_window();
    bool find_next_match(const char* needle, int start_pos, int &match_pos);
    void reset_loaded_document_state(const char* status_text);
    int count_tokens();
};

extern latea* g_wnd;
