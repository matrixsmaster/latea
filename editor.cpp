#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include "common.h"
#include "editor.h"
#include "font_dialog.h"
#include "latea_ui.h"

latea* g_wnd = NULL;

static int line_number_width(int line_count, Fl_Font font, Fl_Fontsize size)
{
    int digits = digit_count(line_count > 0 ? line_count : 1);
    std::string sample((size_t)digits, '8');
    fl_font(font, size);
    return (int)fl_width(sample.c_str()) + 16;
}

void suggestion_state::clear()
{
    text.clear();
    anchor_pos = 0;
    request_id = 0;
    visible = false;
}

latea_editor::latea_editor(int x, int y, int w, int h, const char* label)
    : Fl_Text_Editor(x, y, w, h, label)
{
    textfont(FL_COURIER);
    textsize(16);
}

void latea_editor::draw()
{
    Fl_Text_Editor::draw();
    if (!g_wnd || !g_wnd->suggest.visible) return;
    if (insert_position() != g_wnd->suggest.anchor_pos) return;

    int x = 0;
    int y = 0;
    if (!position_to_xy(g_wnd->suggest.anchor_pos, &x, &y)) return;

    fl_font(textfont(), textsize());
    fl_color(fl_rgb_color(RGB_R(g_wnd->prefs.ghost_color), RGB_G(g_wnd->prefs.ghost_color), RGB_B(g_wnd->prefs.ghost_color)));
    fl_push_clip(this->x(), this->y(), this->w(), this->h());
    fl_draw(g_wnd->suggest.text.c_str(), x, y + textsize());
    fl_pop_clip();
}

int latea_editor::handle(int event)
{
    int old_pos = insert_position();

    if (g_wnd && event == FL_KEYDOWN) {
        int key = Fl::event_key();
        if (key == ' ' && Fl::event_ctrl()) {
            if (g_wnd->cmpt) g_wnd->cmpt->trigger_now();
            return 1;
        }
        if (key == FL_Right && g_wnd->suggest.visible && !Fl::event_ctrl()) {
            g_wnd->accept_suggestion_full();
            return 1;
        }
        if (key == FL_Right && g_wnd->suggest.visible && Fl::event_ctrl()) {
            g_wnd->accept_suggestion_word();
            return 1;
        }
        if (key == FL_Escape && (g_wnd->suggest.visible || g_wnd->cmpt->is_busy())) {
            g_wnd->cmpt->cancel_pending();
            g_wnd->clear_suggestion();
            return 1;
        }
    }

    int rc = Fl_Text_Editor::handle(event);
    if (g_wnd && old_pos != insert_position()) {
        g_wnd->cmpt->on_cursor_changed();
    }
    return rc;
}

static void text_modified_cb(int pos, int nInserted, int nDeleted, int, const char* deletedText, void* cbArg)
{
    latea* wnd = (latea*)cbArg;
    if (!wnd) return;
    wnd->find_selected = false;
    wnd->record_buffer_change(pos, nInserted, nDeleted, deletedText);
    wnd->cmpt->on_text_changed(pos, nInserted, nDeleted, deletedText);
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
    current_font = FL_COURIER;
    current_size = 16;
    line_count_cache = 1;
    line_number_digits = 1;
    find_selected = false;

    prefs.load();
    history.clear();
    suggest.clear();
    select_autocomp();

    g_font_dlg = new font_dialog;
    ui->prefs_window->set_modal();
    ui->font_window->set_modal();
    editor->buffer(textbuf);
    ui->main_window->resizable(editor);

    textbuf->canUndo(0);
    textbuf->add_modify_callback(text_modified_cb, this);

    sync_ui_to_prefs();
    apply_view_preferences();
    update_status("Ready");
    update_ai_status("AI idle");
    new_document();
}

void latea::sync_ui_to_prefs()
{
    ui->dictionary_path_input->value(prefs.dict_path.c_str());

    switch (prefs.autocomp_mode) {
    case AUTOCOMPLETE_DISABLED:
        ui->autocmp_dis->set();
        break;
    case AUTOCOMPLETE_DICTIONARY_FILE:
        ui->autocmp_dict->set();
        break;
    case AUTOCOMPLETE_CURRENT_FILE:
        ui->autocmp_cur->set();
        break;
    case AUTOCOMPLETE_AI:
        ui->autocmp_ai->set();
        break;
    case AUTOCOMPLETE_EMBEDDED_AI:
        ui->autocmp_embedded->set();
        break;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", prefs.max_suggestion_chars);
    ui->max_chars_input->value(buf);
    ui->model_path_input->value(prefs.model_path.c_str());
    ui->ai_host_input->value(prefs.ai_host.c_str());
    snprintf(buf, sizeof(buf), "%d", prefs.ai_port);
    ui->ai_port_input->value(buf);
    ui->endpoint_mode_choice->value(prefs.ai_endpoint_mode);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_delay_ms);
    ui->ai_delay_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_timeout_ms);
    ui->ai_timeout_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_context_length);
    ui->ai_context_input->value(buf);
    snprintf(buf, sizeof(buf), "%.3g", prefs.ai_temperature);
    ui->ai_temperature_input->value(buf);
    snprintf(buf, sizeof(buf), "%.3g", prefs.ai_top_p);
    ui->ai_top_p_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_top_k);
    ui->ai_top_k_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_prefix_chars);
    ui->ai_prefix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_suffix_chars);
    ui->ai_suffix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_slot_id);
    ui->ai_slot_input->value(buf);
    ui->ai_cache_check->value(prefs.ai_cache_prompt ? 1 : 0);
    ui->system_prompt_input->value(prefs.ai_system_prompt.c_str());

    prefs.cont_autocomp ? ui->autocmp_cont->set() : ui->autocmp_cont->clear();
    prefs.word_wrap ? ui->view_wrdwrp->set() : ui->view_wrdwrp->clear();
    prefs.line_numbers ? ui->view_lines->set() : ui->view_lines->clear();
}

void latea::sync_prefs_from_ui()
{
    prefs.dict_path = ui->dictionary_path_input->value();
    prefs.max_suggestion_chars = atoi(ui->max_chars_input->value());
    prefs.model_path = ui->model_path_input->value();
    prefs.ai_host = ui->ai_host_input->value();
    prefs.ai_port = atoi(ui->ai_port_input->value());
    prefs.ai_endpoint_mode = ui->endpoint_mode_choice->value();
    prefs.ai_delay_ms = atoi(ui->ai_delay_input->value());
    prefs.ai_timeout_ms = atoi(ui->ai_timeout_input->value());
    prefs.ai_context_length = atoi(ui->ai_context_input->value());
    prefs.ai_temperature = atof(ui->ai_temperature_input->value());
    prefs.ai_top_p = atof(ui->ai_top_p_input->value());
    prefs.ai_top_k = atoi(ui->ai_top_k_input->value());
    prefs.ai_prefix_chars = atoi(ui->ai_prefix_input->value());
    prefs.ai_suffix_chars = atoi(ui->ai_suffix_input->value());
    prefs.ai_slot_id = atoi(ui->ai_slot_input->value());
    prefs.ai_cache_prompt = ui->ai_cache_check->value() != 0;
    prefs.ai_system_prompt = ui->system_prompt_input->value();
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
    std::string title = EDITOR_TITLE;
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
    ui->ai_status_box->copy_label(text ? text : "");
    ui->ai_status_box->redraw();
}

void latea::show_find_dialog(bool replace_mode)
{
    if (replace_mode) {
        ui->find_window->copy_label("Find / Replace");
        ui->replace_group->show();
        ui->replace_button->show();
        ui->replace_all_button->show();
    } else {
        ui->find_window->copy_label("Find");
        ui->replace_group->hide();
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
    current_size = prefs.text_size > 0 ? prefs.text_size : 16;
    if (prefs.text_font_name.empty()) current_font = FL_COURIER;
    else {
        Fl::set_font(FL_FREE_FONT, prefs.text_font_name.c_str());
        current_font = FL_FREE_FONT;
    }

    editor->textfont(current_font);
    editor->textsize(current_size);
    editor->linenumber_font(current_font);
    editor->linenumber_size(current_size);

    Fl_Color text_color = fl_rgb_color(RGB_R(prefs.text_color), RGB_G(prefs.text_color), RGB_B(prefs.text_color));
    Fl_Color bg = fl_rgb_color(RGB_R(prefs.bg_color), RGB_G(prefs.bg_color), RGB_B(prefs.bg_color));
    Fl_Color sel_color = fl_rgb_color(RGB_R(prefs.sel_color), RGB_G(prefs.sel_color), RGB_B(prefs.sel_color));
    Fl_Color linenumber_bg = fl_rgb_color(RGB_R(prefs.line_bgcol), RGB_G(prefs.line_bgcol), RGB_B(prefs.line_bgcol));
    Fl_Color linenumber_fg = fl_rgb_color(RGB_R(prefs.line_fgcol), RGB_G(prefs.line_fgcol), RGB_B(prefs.line_fgcol));
    editor->color(bg);
    editor->textcolor(text_color);
    editor->cursor_color(text_color);
    editor->selection_color(sel_color);
    editor->linenumber_bgcolor(linenumber_bg);
    editor->linenumber_fgcolor(linenumber_fg);

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
    sync_ui_to_prefs();
    cmpt_dict.on_preferences_changed();
    cmpt_file.on_preferences_changed();
    cmpt_ai.on_preferences_changed();
    cmpt_embedded_ai.on_preferences_changed();
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
        cmpt = &cmpt_embedded_ai;
        break;
    default:
        cmpt = &cmpt_dict;
        break;
    }
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

void latea::set_suggestion(const std::string &text, int anchor_pos, int request_id)
{
    std::string clean = sanitize_suggestion(text, prefs.max_suggestion_chars);
    if (clean.empty()) {
        clear_suggestion();
        return;
    }
    suggest.text = clean;
    suggest.anchor_pos = anchor_pos;
    suggest.request_id = request_id;
    suggest.visible = true;
    editor->redraw();
}

void latea::clear_suggestion()
{
    if (!suggest.visible) return;
    suggest.clear();
    editor->redraw();
}

void latea::accept_suggestion_full()
{
    if (!suggest.visible) return;
    int pos = editor->insert_position();
    std::string insert = suggest.text;
    cmpt->cancel_pending();
    clear_suggestion();
    textbuf->insert(pos, insert.c_str());
    editor->insert_position(pos + (int)insert.size());
    editor->show_insert_position();
    cmpt->schedule();
}

void latea::accept_suggestion_word()
{
    if (!suggest.visible) return;
    std::string part = suggest.text;
    size_t split = part.find_first_of(" \t");
    if (split != std::string::npos) part.erase(split);
    int pos = editor->insert_position();
    cmpt->cancel_pending();
    clear_suggestion();
    textbuf->insert(pos, part.c_str());
    editor->insert_position(pos + (int)part.size());
    editor->show_insert_position();
    cmpt->schedule();
}

void latea::record_buffer_change(int pos, int inserted, int deleted, const char* deleted_text)
{
    (void)deleted;
    if (suppress_history) return;

    char* inserted_text = inserted > 0 ? textbuf->text_range(pos, pos + inserted) : 0;
    std::string inserted_string = inserted_text ? inserted_text : "";

    if (prefs.line_numbers) {
        line_count_cache += count_newlines(inserted_string.c_str()) - count_newlines(deleted_text);
        if (line_count_cache < 1) line_count_cache = 1;
        int digits = digit_count(line_count_cache);
        if (digits != line_number_digits) set_line_numbers(true);
    }

    if (!history.replaying) {
        edit_action action;
        action.pos = pos;
        action.removed = deleted_text ? deleted_text : "";
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

void latea::close_main_window()
{
    if (!save_if_needed()) return;
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
    line_count_cache = textbuf->count_lines(0, textbuf->length()) + 1;
    if (line_count_cache < 1) line_count_cache = 1;
    find_selected = false;
    clear_suggestion();
    set_changed(false);
    set_line_numbers(prefs.line_numbers);
    editor->insert_position(0);
    editor->show_insert_position();
    cmpt->on_preferences_changed();
    update_status(status_text);
    update_ai_status("AI idle");
}

int main(int argc, char* argv[])
{
    Fl::lock();
    g_wnd = new latea();
    g_wnd->ui->show();
    if (argc > 1) g_wnd->open_file(argv[1]);
    return Fl::run();
}
