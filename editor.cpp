#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include "common.h"
#include "editor.h"
#include "font_dialog.h"
#include "prefs_dlg.h"
#include "latea_ui.h"

using namespace std;

latea* g_wnd = NULL;

static int line_number_width(int line_count, Fl_Font font, Fl_Fontsize size)
{
    int digits = digit_count(line_count > 0 ? line_count : 1);
    string sample((size_t)digits, '8');
    fl_font(font, size);
    return (int)fl_width(sample.c_str()) + 16;
}

void suggestion_state::clear()
{
    text.clear();
    vars.clear();
    anchor_pos = 0;
    request_id = 0;
    var_idx = 0;
    visible = false;
}

latea_editor::latea_editor(int x, int y, int w, int h, const char* label)
    : Fl_Text_Editor(x, y, w, h, label)
{
    textfont(DEF_TEXT_FONT);
    textsize(DEF_TEXT_SIZE);
}

void latea_editor::draw()
{
    int x = 0, y = 0;
    int need_lines;
    int box_h;
    int first_w;
    int full_w;
    int i;
    size_t p;
    vector<string> lines;
    vector<int> line_x;
    vector<int> line_w;
    Fl_Color bg;
    Fl_Color fg;

    Fl_Text_Editor::draw();
    if (!g_wnd || !g_wnd->suggest.visible) return;
    if (insert_position() != g_wnd->suggest.anchor_pos) return;
    if (!position_to_xy(g_wnd->suggest.anchor_pos, &x, &y)) return;

    fl_font(textfont(), textsize());
    int line_h = (int)fl_height();

    first_w = text_area.x + text_area.w - x;
    full_w = text_area.w;
    if (first_w < 1) first_w = 1;
    if (full_w < 1) full_w = 1;
    lines.clear();
    line_x.clear();
    line_w.clear();
    p = 0;
    while (p < g_wnd->suggest.text.size()) {
        string line;
        int cur_w;
        double w = 0;

        cur_w = lines.empty() ? first_w : full_w;
        while (p < g_wnd->suggest.text.size()) {
            char c = g_wnd->suggest.text[p];
            double cw = fl_width((unsigned int)(unsigned char)c);

            if (!line.empty() && w + cw > cur_w) break;
            line += c;
            w += cw;
            p++;
            if (w > cur_w) break;
        }
        if (line.empty() && p < g_wnd->suggest.text.size()) {
            line += g_wnd->suggest.text[p];
            p++;
        }
        lines.push_back(line);
        line_x.push_back(lines.size() == 1 ? x : text_area.x);
        line_w.push_back(cur_w);
    }
    if (lines.empty()) {
        lines.push_back("");
        line_x.push_back(x);
        line_w.push_back(first_w);
    }

    box_h = line_h * (int)lines.size();
    if (y + box_h > text_area.y + text_area.h && g_wnd->prefs.word_wrap) {
        need_lines = (y + box_h - text_area.y - text_area.h + line_h - 1) / line_h;
        if (need_lines < 1) need_lines = 1;
        scroll(mTopLineNum + need_lines, mHorizOffset);
        if (!position_to_xy(g_wnd->suggest.anchor_pos, &x, &y)) return;
        if (++scrolled <= EDITOR_SCROLL_ATTEMPTS) {
            draw();
            return;
        }
    }
    scrolled = 0;

    bg = fl_rgb_color(RGB_R(g_wnd->prefs.bg_color), RGB_G(g_wnd->prefs.bg_color), RGB_B(g_wnd->prefs.bg_color));
    fg = fl_rgb_color(RGB_R(g_wnd->prefs.ghost_color), RGB_G(g_wnd->prefs.ghost_color), RGB_B(g_wnd->prefs.ghost_color));

    fl_push_clip(this->x(), this->y(), this->w(), this->h());
    for (i = 0; i < (int)lines.size(); i++) {
        fl_color(bg);
        fl_rectf(line_x[(size_t)i], y + i * line_h, line_w[(size_t)i], line_h);
        fl_color(fg);
        fl_draw(lines[(size_t)i].c_str(), line_x[(size_t)i], y + (i + 1) * line_h - fl_descent());
    }
    fl_pop_clip();
}

int latea_editor::handle(int event)
{
    int old_pos = insert_position();
    int old_rev = g_wnd->text_rev;

    if (event == FL_KEYDOWN) {
        int key = Fl::event_key();
        if (key == FL_Right && g_wnd->suggest.visible && !Fl::event_ctrl()) {
            g_wnd->accept_suggestion_full();
            return 1;
        }
        if (key == FL_Right && g_wnd->suggest.visible && Fl::event_ctrl()) {
            g_wnd->accept_suggestion_word();
            return 1;
        }
        if (key == FL_Down && g_wnd->suggest.visible) {
            g_wnd->next_suggestion();
            return 1;
        }
        if (key == FL_Up && g_wnd->suggest.visible) {
            g_wnd->prev_suggestion();
            return 1;
        }
        if (key == FL_Escape && (g_wnd->suggest.visible || g_wnd->cmpt->is_busy())) {
            g_wnd->cmpt->cancel_pending();
            g_wnd->clear_suggestion();
            return 1;
        }
    }

    int rc = Fl_Text_Editor::handle(event);
    if (old_pos != insert_position()) {
        g_wnd->update_caret_pos();
        if (old_rev == g_wnd->text_rev) g_wnd->cmpt->on_cursor_changed();
    }
    return rc;
}

static void text_modified_cb(int pos, int nInserted, int nDeleted, int, const char* deletedText, void*)
{
    g_wnd->find_selected = false;
    g_wnd->text_rev++;
    g_wnd->record_buffer_change(pos, nInserted, nDeleted, deletedText);
    g_wnd->update_caret_pos();
    g_wnd->cmpt->on_text_changed(pos, nInserted, nDeleted, deletedText);
}

latea::latea()
{
    g_wnd = this;
    ui = new LateaUI;
    editor = ui->editor;
    textbuf = new Fl_Text_Buffer;
    cmpt = &cmpt_dict;
    changed = false;
    suppress_history = 0;
    suppress_autocomp = 0;
    current_font = DEF_TEXT_FONT;
    current_size = DEF_TEXT_SIZE;
    line_count_cache = 1;
    line_number_digits = 1;
    find_selected = false;
    ai_used_toks = 0;
    ai_used_ctx = 0;
    text_rev = 0;
    app_icon = NULL;
    about_img = NULL;

    prefs.load();
    history.clear();
    suggest.clear();
    select_autocomp();

    g_font_dlg = new font_dialog;
    g_prefs_dlg = new prefs_dialog;

    app_icon = new Fl_PNG_Image(APP_ICON_FILE);
    if (app_icon->w() > 0 && app_icon->h() > 0) {
        Fl_Window::default_icon(app_icon);
        //Fl_Double_Window* wins[] = EDITOR_WINDOWS;
        //for (size_t i = 0; i < NUMITEMS(wins); i++) wins[i]->icon(app_icon);
        about_img = app_icon->copy(ui->about_icon->w(),ui->about_icon->h());
        ui->about_icon->image(about_img);
    }
    if (prefs.win_w > 0 && prefs.win_h > 0) ui->main_window->size(prefs.win_w, prefs.win_h);
    editor->buffer(textbuf);
    ui->main_window->resizable(editor);

    textbuf->canUndo(0);
    textbuf->add_modify_callback(text_modified_cb, this);

    g_prefs_dlg->prefs = prefs;
    g_prefs_dlg->sync_ui();
    apply_view_preferences();
    update_status("Ready");
    update_ai_status("");
    new_document();
}

void latea::new_document()
{
    if (!save_if_needed()) return;
    suppress_history++;
    textbuf->text("");
    suppress_history--;
    filename.clear();
    reset_loaded_document_state("New document");
}

bool latea::open_file(const char* path)
{
    if (!path) {
        Fl_Native_File_Chooser c;
        c.title("Open File");
        c.type(Fl_Native_File_Chooser::BROWSE_FILE);
        if (c.show()) return false;
        return open_file(c.filename());
    }

    if (!save_if_needed()) return false;

    suppress_history++;
    int rc = textbuf->loadfile(path);
    suppress_history--;
    if (rc != 0) {
        fl_alert("Cannot open '%s': %s", path, strerror(errno));
        update_status("Open failed");
        return false;
    }

    filename = path;
    reset_loaded_document_state("File opened");
    return true;
}

bool latea::save_file()
{
    if (filename.empty()) return save_file_as();
    int rc = textbuf->savefile(filename.c_str());
    if (rc != 0) {
        fl_alert("Cannot save '%s': %s", filename.c_str(), strerror(errno));
        update_status("Save failed");
        return false;
    }
    set_changed(false);
    update_status("File saved");
    return true;
}

bool latea::save_file_as()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Save As");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    if (!filename.empty()) chooser.preset_file(filename.c_str());
    if (chooser.show()) return false;
    filename = chooser.filename();
    return save_file();
}

bool latea::save_if_needed()
{
    if (!changed) return true;
    int rc = fl_choice("The document has unsaved changes.", "Cancel", "Save", "Discard");
    if (rc == 0) return false;
    if (rc == 1) return save_file();
    return true;
}

void latea::update_title()
{
    string title = TITLE;
    if (!filename.empty()) title += " - " + filename;
    if (changed) title += " *";
    ui->main_window->copy_label(title.c_str());
}

void latea::update_status(const char* text)
{
    ui->status_box->copy_label(text ? text : "");
    ui->status_box->redraw();
}

void latea::update_ai_status(const char* text)
{
    ai_status_text = text ? text : "";
    refresh_ai_status();
}

void latea::show_find_dialog(bool replace_mode)
{
    if (replace_mode) {
        ui->find_window->copy_label("Find / Replace");
        ui->replace_text_input->show();
        ui->replace_button->show();
        ui->replace_all_button->show();
    } else {
        ui->find_window->copy_label("Find");
        ui->replace_text_input->hide();
        ui->replace_button->hide();
        ui->replace_all_button->hide();
    }
    ui->find_window->show();
    ui->find_text_input->take_focus();
}

void latea::choose_font()
{
    g_font_dlg->open();
}

void latea::browse_dictionary_path()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose Vocabulary File");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    if (ui->dictionary_path_input->value()[0]) chooser.preset_file(ui->dictionary_path_input->value());
    if (chooser.show() == 0) ui->dictionary_path_input->value(chooser.filename());
}

void latea::browse_model_path()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose Model File");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    if (ui->model_path_input->value()[0]) chooser.preset_file(ui->model_path_input->value());
    if (chooser.show() == 0) ui->model_path_input->value(chooser.filename());
}

void latea::browse_llama_path()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose llama.cpp Server");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    if (ui->llama_path_input->value()[0]) chooser.preset_file(ui->llama_path_input->value());
    if (chooser.show() == 0) ui->llama_path_input->value(chooser.filename());
}

void latea::find_next()
{
    const char* needle = ui->find_text_input->value();
    if (!needle || !needle[0]) return;

    int pos = 0;
    suppress_autocomp++;
    if (!find_next_match(needle, editor->insert_position(), pos)) {
        suppress_autocomp--;
        update_status("Text not found");
        return;
    }

    int end = pos + (int)strlen(needle);
    textbuf->select(pos, end);
    editor->insert_position(end);
    editor->show_insert_position();
    find_selected = true;
    clear_suggestion();
    suppress_autocomp--;
    update_status("Match found");
}

void latea::replace_next()
{
    const char* needle = ui->find_text_input->value();
    const char* replacement = ui->replace_text_input->value();
    if (!needle || !needle[0]) return;

    int pos = 0;
    int end = 0;
    suppress_autocomp++;
    if (find_selected) {
        int sel_start = 0;
        int sel_end = 0;
        char* selected_text = NULL;

        if (textbuf->selection_position(&sel_start, &sel_end) && sel_end - sel_start == (int)strlen(needle)) {
            selected_text = textbuf->selection_text();
            if (selected_text && strcmp(selected_text, needle) == 0) {
                pos = sel_start;
                end = sel_end;
            }
        }
        free(selected_text);
    }
    if (end <= pos) {
        if (!find_next_match(needle, editor->insert_position(), pos)) {
            find_selected = false;
            suppress_autocomp--;
            update_status("Text not found");
            return;
        }
        end = pos + (int)strlen(needle);
    }

    textbuf->replace(pos, end, replacement);
    end = pos + (int)strlen(replacement);
    textbuf->select(pos, end);
    editor->insert_position(end);
    editor->show_insert_position();
    find_selected = false;
    clear_suggestion();
    suppress_autocomp--;
    update_status("Replaced next match");
}

void latea::replace_all()
{
    const char* needle = ui->find_text_input->value();
    const char* replacement = ui->replace_text_input->value();
    if (!needle || !needle[0]) return;

    int count = 0;
    int pos = 0;
    suppress_autocomp++;
    while (textbuf->search_forward(pos, needle, &pos)) {
        textbuf->replace(pos, pos + (int)strlen(needle), replacement);
        pos += (int)strlen(replacement);
        count++;
    }
    find_selected = false;
    clear_suggestion();
    suppress_autocomp--;

    char msg[128];
    snprintf(msg, sizeof(msg), "Replaced %d occurrence(s)", count);
    update_status(msg);
}

void latea::apply_view_preferences()
{
    current_size = prefs.text_size > 0 ? prefs.text_size : DEF_TEXT_SIZE;
    if (prefs.text_font_name.empty()) current_font = DEF_TEXT_FONT;
    else {
        int font_count = Fl::set_fonts("*");

        current_font = DEF_TEXT_FONT;
        for (int i = FL_FREE_FONT; i < font_count; i++) {
            int attr = 0;
            const char* name = Fl::get_font_name((Fl_Font)i, &attr);

            if (!name || prefs.text_font_name != name) continue;
            current_font = (Fl_Font)i;
            break;
        }
        if (current_font == DEF_TEXT_FONT) {
            Fl::set_font(FL_FREE_FONT, prefs.text_font_name.c_str());
            current_font = FL_FREE_FONT;
        }
    }

    editor->textfont(current_font);
    editor->textsize(current_size);
    editor->linenumber_font(current_font);
    editor->linenumber_size(current_size);

    Fl_Color text_color = fl_rgb_color(RGB_R(prefs.text_color), RGB_G(prefs.text_color), RGB_B(prefs.text_color));
    Fl_Color bg = fl_rgb_color(RGB_R(prefs.bg_color), RGB_G(prefs.bg_color), RGB_B(prefs.bg_color));
    Fl_Color sel_color = fl_rgb_color(RGB_R(prefs.sel_color), RGB_G(prefs.sel_color), RGB_B(prefs.sel_color));
    editor->color(bg);
    editor->textcolor(text_color);
    editor->cursor_color(text_color);
    editor->selection_color(sel_color);

    set_word_wrap(prefs.word_wrap);
    set_line_numbers(prefs.line_numbers);
    editor->damage(FL_DAMAGE_ALL);
    editor->redisplay_range(0, textbuf->length());
    editor->redraw();
}

void latea::apply_preferences()
{
    prefs.save();
    apply_view_preferences();
    g_prefs_dlg->prefs = prefs;
    g_prefs_dlg->sync_ui();
    cmpt_dict.on_preferences_changed();
    cmpt_file.on_preferences_changed();
    cmpt_ai.on_preferences_changed();
    cmpt_emb_ai.on_preferences_changed();
    select_autocomp();
    update_status("Config saved");
}

void latea::set_autocomp_mode(int mode)
{
    prefs.autocomp_mode = mode;
    apply_preferences();
}

void latea::select_autocomp()
{
    switch (prefs.autocomp_mode) {
    case AUTOCOMPLETE_DICTIONARY_FILE:
        cmpt = &cmpt_dict;
        break;
    case AUTOCOMPLETE_CURRENT_FILE:
        cmpt = &cmpt_file;
        break;
    case AUTOCOMPLETE_AI:
        cmpt = &cmpt_ai;
        break;
    case AUTOCOMPLETE_EMBEDDED_AI:
        cmpt = &cmpt_emb_ai;
        break;
    default:
        cmpt = &cmpt_dict;
        break;
    }
    if (prefs.autocomp_mode != AUTOCOMPLETE_EMBEDDED_AI) update_ai_usage(0, 0);
}

void latea::set_word_wrap(bool enabled)
{
    prefs.word_wrap = enabled;
    if (enabled) editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    else editor->wrap_mode(Fl_Text_Display::WRAP_NONE, 0);
}

void latea::set_line_numbers(bool enabled)
{
    prefs.line_numbers = enabled;
    if (!enabled) {
        editor->linenumber_width(0);
        return;
    }

    if (line_count_cache < 1) line_count_cache = 1;
    line_number_digits = digit_count(line_count_cache);
    editor->linenumber_width(line_number_width(line_count_cache, current_font, current_size));
}

void latea::show_stats()
{
    char* text = textbuf->text();
    int lines = 1;
    int maxlen = 0;
    int words = 0;
    int chars = 0;
    int bytes = 0;
    int toks = 0;
    int line_len = 0;
    int in_word = 0;

    if (!text) return;
    for (char* p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;

        bytes++;
        if ((c & 0xc0) != 0x80) chars++;
        if (c == '\n') {
            if (line_len > maxlen) maxlen = line_len;
            line_len = 0;
            lines++;
        } else {
            line_len++;
        }
        if (isspace(c)) in_word = 0;
        else if (!in_word) {
            words++;
            in_word = 1;
        }
    }
    if (line_len > maxlen) maxlen = line_len;

    ui->stats_lines_out->value(lines);
    ui->stats_maxlen_out->value(maxlen);
    ui->stats_words_out->value(words);
    ui->stats_toks_out->value(toks);
    ui->stats_chars_out->value(chars);
    ui->stats_bytes_out->value(bytes);
    ui->stats_window->show();
    free(text);
}

void latea::reset_autocomp()
{
    cmpt_dict.cancel_pending();
    cmpt_file.cancel_pending();
    cmpt_ai.cancel_pending();
    cmpt_emb_ai.cancel_pending();
    llama_client::stop_server();
    cmpt_emb_ai.on_preferences_changed();
    suggest.clear();
    update_ai_usage(0, 0);
    editor->redraw();
    update_ai_status("Autocomplete reset");
}

void latea::set_suggestion(const string &text, int anchor_pos, int request_id)
{
    string clean = sanitize_suggestion(text, prefs.max_suggestion_chars);
    if (clean.empty()) {
        clear_suggestion();
        return;
    }
    if (suggest.visible && suggest.anchor_pos == anchor_pos && suggest.text == clean) {
        suggest.request_id = request_id;
        editor->redraw();
        return;
    }
    if (request_id && suggest.visible && suggest.anchor_pos == anchor_pos && !suggest.vars.empty()) {
        if (suggest.vars[suggest.var_idx] != clean) suggest.vars.push_back(clean);
        suggest.var_idx = (int)suggest.vars.size() - 1;
        suggest.text = suggest.vars[suggest.var_idx];
        suggest.request_id = request_id;
        suggest.visible = true;
        editor->redraw();
        return;
    }
    suggest.text = clean;
    suggest.vars.clear();
    suggest.vars.push_back(clean);
    suggest.anchor_pos = anchor_pos;
    suggest.request_id = request_id;
    suggest.var_idx = 0;
    suggest.visible = true;
    editor->redraw();
}

void latea::set_suggestion_list(vector<string> vars, int var_idx, int anchor_pos, int request_id)
{
    suggest.clear();
    for (size_t i = 0; i < vars.size(); i++) {
        string clean = sanitize_suggestion(vars[i], prefs.max_suggestion_chars);
        if (!clean.empty()) suggest.vars.push_back(clean);
    }
    if (suggest.vars.empty()) {
        editor->redraw();
        return;
    }
    if (var_idx < 0) var_idx = 0;
    if (var_idx >= (int)suggest.vars.size()) var_idx = (int)suggest.vars.size() - 1;
    suggest.text = suggest.vars.at(var_idx);
    suggest.anchor_pos = anchor_pos;
    suggest.request_id = request_id;
    suggest.var_idx = var_idx;
    suggest.visible = true;
    editor->redraw();
}

void latea::clear_suggestion()
{
    if (!suggest.visible) return;
    suggest.clear();
    editor->redraw();
}

void latea::prev_suggestion()
{
    if (cmpt) cmpt->move_suggestion(-1);
}

void latea::next_suggestion()
{
    if (cmpt) cmpt->move_suggestion(1);
}

void latea::accept_suggestion_full()
{
    if (!suggest.visible) return;
    int pos = editor->insert_position();
    string insert = suggest.text;
    cmpt->cancel_pending();
    clear_suggestion();
    textbuf->insert(pos, insert.c_str());
    editor->insert_position(pos + (int)insert.size());
    editor->show_insert_position();
    cmpt->schedule();
}

void latea::accept_suggestion_word()
{
    size_t start;
    size_t stop;
    string part;
    string rest;
    int pos;

    if (!suggest.visible) return;
    start = 0;
    while (start < suggest.text.size() && (suggest.text[start] == ' ' || suggest.text[start] == '\t')) start++;
    stop = start;
    while (stop < suggest.text.size() && is_word_char(suggest.text[stop])) stop++;
    if (stop <= start) {
        stop = 0;
        while (stop < suggest.text.size() && (suggest.text[stop] == ' ' || suggest.text[stop] == '\t')) stop++;
        if (stop < suggest.text.size()) stop++;
    }
    part = suggest.text.substr(0, stop);
    rest = suggest.text.substr(stop);
    pos = editor->insert_position();
    cmpt->cancel_pending();
    if (part.empty()) return;
    suggest.clear();
    editor->redraw();
    textbuf->insert(pos, part.c_str());
    editor->insert_position(pos + (int)part.size());
    editor->show_insert_position();
    if (!rest.empty()) set_suggestion(rest, pos + (int)part.size(), 0);
    cmpt->schedule();
}

void latea::record_buffer_change(int pos, int inserted, int deleted, const char* deleted_text)
{
    const char* removed = deleted_text ? deleted_text : "";
    if (suppress_history) return;
    if (inserted <= 0 && deleted <= 0 && !removed[0]) return;

    char* inserted_text = inserted > 0 ? textbuf->text_range(pos, pos + inserted) : 0;
    string inserted_string = inserted_text ? inserted_text : "";

    line_count_cache += count_newlines(inserted_string.c_str()) - count_newlines(removed);
    if (line_count_cache < 1) line_count_cache = 1;
    if (prefs.line_numbers && digit_count(line_count_cache) != line_number_digits) set_line_numbers(true);

    if (!history.replaying) {
        edit_action action;
        action.pos = pos;
        action.removed = removed;
        action.inserted = inserted_string;
        action.cursor_before = pos;
        action.cursor_after = pos + inserted;
        history.record(action);
        set_changed(true);
    }

    free(inserted_text);
}

void latea::set_changed(bool value)
{
    changed = value;
    update_title();
}

void latea::update_caret_pos()
{
    int pos = editor->insert_position();
    int line = textbuf->count_lines(0, pos) + 1;
    int col = pos - editor->line_start(pos) + 1;
    char buf[64];

    snprintf(buf, sizeof(buf), "%d : %d", line, col);
    ui->caret_box->copy_label(buf);
    ui->caret_box->redraw();
}

void latea::update_ai_usage(int used, int ctx)
{
    ai_used_toks = used;
    ai_used_ctx = ctx;
    refresh_ai_status();
}

void latea::refresh_ai_status()
{
    char buf[256];

    if (ai_used_ctx > 0) snprintf(buf, sizeof(buf), "%s %d/%d", ai_status_text.c_str(), ai_used_toks, ai_used_ctx);
    else snprintf(buf, sizeof(buf), "%s", ai_status_text.c_str());
    ui->ai_status_box->copy_label(buf);
    ui->ai_status_box->redraw();
}

void latea::close_main_window()
{
    if (!save_if_needed()) return;
    prefs.win_w = ui->main_window->w();
    prefs.win_h = ui->main_window->h();
    prefs.save();
    llama_client::stop_server();
    ui->main_window->hide();
}

bool latea::find_next_match(const char* needle, int start_pos, int &match_pos)
{
    if (!needle || !needle[0]) return false;
    if (textbuf->search_forward(start_pos, needle, &match_pos)) return true;
    return textbuf->search_forward(0, needle, &match_pos) != 0;
}

void latea::reset_loaded_document_state(const char* status_text)
{
    history.clear();
    line_count_cache = MAX(1, textbuf->count_lines(0, textbuf->length()) + 1);
    find_selected = false;
    clear_suggestion();
    set_changed(false);
    set_line_numbers(prefs.line_numbers);
    editor->insert_position(0);
    editor->show_insert_position();
    update_caret_pos();
    update_ai_usage(0, 0);
    cmpt->on_preferences_changed();
    update_status(status_text);
    update_ai_status("");
}

int latea::count_tokens()
{
    if (prefs.model_path.empty()) return 0;

    model_state st;
    if (!st.m.open_mmap(prefs.model_path.c_str())) {
        ai_status_text = "Can't open the model file";
        return 0;
    }
    if (!st.m.read_gguf()) {
        ai_status_text = "Incompatible GGUF model";
        return 0;
    }
    if (!st.m.read_tokenizer()) {
        ai_status_text = "No tokenizer in GGUF model";
        return 0;
    }

    int res = tokenize(&st, textbuf->text(), true, false).size();

    st.m.close_mmap();
    refresh_ai_status();
    return res;
}

int main(int argc, char* argv[])
{
    Fl::lock();
    g_wnd = new latea();
    g_wnd->ui->show();
    if (argc > 1) g_wnd->open_file(argv[1]);
    return Fl::run();
}
