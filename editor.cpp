#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include "common.h"
#include "editor.h"
#include "latea_ui.h"

editor_window* g_wnd = NULL;

static int line_number_width(Fl_Text_Buffer *buffer, Fl_Font font, Fl_Fontsize size)
{
    int lines = buffer ? buffer->count_lines(0, buffer->length()) + 1 : 1;
    int digits = 1;
    for (int n = lines; n >= 10; n /= 10) digits++;

    std::string sample((size_t)digits, '8');
    fl_font(font, size);
    return (int)fl_width(sample.c_str()) + 16;
}

static void update_font_preview(editor_window *window)
{
    int font_index = window->ui->font_browser->value() - 1;
    if (font_index < 0 || font_index >= (int)window->font_dialog_fonts.size()) return;
    int size = atoi(window->ui->font_size_input->value());
    if (size <= 0) size = 16;
    window->ui->font_preview->copy_label("The quick brown fox jumps over the lazy dog");
    window->ui->font_preview->labelfont(window->font_dialog_fonts[font_index]);
    window->ui->font_preview->labelsize(size);
    window->ui->font_preview->labelcolor(window->font_dialog_selected_color);
    window->ui->font_preview->redraw();
    window->ui->font_color_button->color(window->font_dialog_selected_color);
    window->ui->font_color_button->redraw();
}

static void populate_size_browser(editor_window *window, int index)
{
    window->ui->font_size_browser->clear();
    window->font_dialog_sizes[index].clear();

    int *sizes = 0;
    int count = Fl::get_font_sizes(window->font_dialog_fonts[index], sizes);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", sizes[i]);
            window->ui->font_size_browser->add(buf);
            window->font_dialog_sizes[index].push_back(sizes[i]);
        }
    } else {
        static const int defaults[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32};
        for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", defaults[i]);
            window->ui->font_size_browser->add(buf);
            window->font_dialog_sizes[index].push_back(defaults[i]);
        }
    }
}

void suggestion_state::clear()
{
    text.clear();
    anchor_pos = 0;
    request_id = 0;
    visible = 0;
}

latea_editor::latea_editor(int x, int y, int w, int h, const char *label)
    : Fl_Text_Editor(x, y, w, h, label)
{
    textfont(FL_COURIER);
    textsize(16);
}

void latea_editor::draw()
{
    Fl_Text_Editor::draw();
    if (!g_wnd || !g_wnd->suggestion.visible) return;
    if (insert_position() != g_wnd->suggestion.anchor_pos) return;

    int x = 0;
    int y = 0;
    if (!position_to_xy(g_wnd->suggestion.anchor_pos, &x, &y)) return;

    fl_font(textfont(), textsize());
    fl_color(FL_DARK3);
    fl_push_clip(this->x(), this->y(), this->w(), this->h());
    fl_draw(g_wnd->suggestion.text.c_str(), x, y + textsize());
    fl_pop_clip();
}

int latea_editor::handle(int event)
{
    int old_pos = insert_position();

    if (g_wnd && g_wnd->suggestion.visible && event == FL_KEYDOWN) {
        int key = Fl::event_key();
        if (key == FL_Right && !Fl::event_ctrl()) {
            g_wnd->accept_suggestion_full();
            return 1;
        }
        if (key == FL_Right && Fl::event_ctrl()) {
            g_wnd->accept_suggestion_word();
            return 1;
        }
        if (key == FL_Escape) {
            g_wnd->clear_suggestion();
            return 1;
        }
    }

    int rc = Fl_Text_Editor::handle(event);
    if (g_wnd && old_pos != insert_position()) {
        g_wnd->last_cursor_pos = insert_position();
        g_wnd->autocomplete.on_cursor_changed();
    }
    return rc;
}

editor_window::editor_window()
{
    ui = new LateaUI;
    editor = ui->editor;
    textbuf = new Fl_Text_Buffer;
    changed = 0;
    suppress_history = 0;
    last_cursor_pos = 0;
    current_font = FL_COURIER;
    current_size = 16;

    prefs.load();
    history.clear();
    suggestion.clear();
    autocomplete.set_owner(this);

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

editor_window::~editor_window()
{
    delete textbuf;
    delete ui;
}

void editor_window::show(int argc, char **argv)
{
    if (argc > 1) open_file(argv[1]);
    ui->show();
}

void editor_window::sync_ui_to_prefs()
{
    ui->dictionary_path_input->value(prefs.dictionary_path.c_str());
    ui->autocomplete_mode_choice->value(prefs.autocomplete_mode);
    ui->continuous_check->value(prefs.continuous_autocomplete);

    switch (prefs.autocomplete_mode) {
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
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", prefs.max_suggestion_chars);
    ui->max_chars_input->value(buf);
    ui->ai_host_input->value(prefs.ai_host.c_str());
    snprintf(buf, sizeof(buf), "%d", prefs.ai_port);
    ui->ai_port_input->value(buf);
    ui->endpoint_mode_choice->value(prefs.ai_endpoint_mode);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_delay_ms);
    ui->ai_delay_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_timeout_ms);
    ui->ai_timeout_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_prefix_chars);
    ui->ai_prefix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_suffix_chars);
    ui->ai_suffix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", prefs.ai_slot_id);
    ui->ai_slot_input->value(buf);
    ui->ai_cache_check->value(prefs.ai_cache_prompt);
    ui->system_prompt_input->value(prefs.ai_system_prompt.c_str());

    prefs.continuous_autocomplete ? ui->autocmp_cont->set() : ui->autocmp_cont->clear();
    prefs.word_wrap ? ui->view_wrdwrp->set() : ui->view_wrdwrp->clear();
    prefs.line_numbers ? ui->view_lines->set() : ui->view_lines->clear();
}

void editor_window::sync_prefs_from_ui()
{
    prefs.dictionary_path = ui->dictionary_path_input->value();
    prefs.autocomplete_mode = ui->autocomplete_mode_choice->value();
    prefs.continuous_autocomplete = ui->continuous_check->value();
    prefs.max_suggestion_chars = atoi(ui->max_chars_input->value());
    prefs.ai_host = ui->ai_host_input->value();
    prefs.ai_port = atoi(ui->ai_port_input->value());
    prefs.ai_endpoint_mode = ui->endpoint_mode_choice->value();
    prefs.ai_delay_ms = atoi(ui->ai_delay_input->value());
    prefs.ai_timeout_ms = atoi(ui->ai_timeout_input->value());
    prefs.ai_prefix_chars = atoi(ui->ai_prefix_input->value());
    prefs.ai_suffix_chars = atoi(ui->ai_suffix_input->value());
    prefs.ai_slot_id = atoi(ui->ai_slot_input->value());
    prefs.ai_cache_prompt = ui->ai_cache_check->value();
    prefs.ai_system_prompt = ui->system_prompt_input->value();
}

void editor_window::new_document()
{
    if (!save_if_needed()) return;
    suppress_history++;
    textbuf->text("");
    suppress_history--;
    history.clear();
    filename.clear();
    clear_suggestion();
    set_changed(0);
    set_line_numbers(prefs.line_numbers);
    editor->insert_position(0);
    editor->show_insert_position();
    update_status("New document");
    update_ai_status("AI idle");
}

bool editor_window::open_file(const char *path)
{
    if (!path) return false;
    if (!save_if_needed()) return false;

    suppress_history++;
    int rc = textbuf->loadfile(path);
    suppress_history--;
    if (rc != 0) {
        fl_alert("Cannot open '%s': %s", path, strerror(errno));
        update_status("Open failed");
        return false;
    }

    history.clear();
    filename = path;
    clear_suggestion();
    set_changed(0);
    set_line_numbers(prefs.line_numbers);
    editor->insert_position(0);
    editor->show_insert_position();
    autocomplete.on_preferences_changed();
    update_status("File opened");
    update_ai_status("AI idle");
    return true;
}

bool editor_window::save_file()
{
    if (filename.empty()) return save_file_as();
    int rc = textbuf->savefile(filename.c_str());
    if (rc != 0) {
        fl_alert("Cannot save '%s': %s", filename.c_str(), strerror(errno));
        update_status("Save failed");
        return false;
    }
    set_changed(0);
    update_status("File saved");
    return true;
}

bool editor_window::save_file_as()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Save As");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    if (!filename.empty()) chooser.preset_file(filename.c_str());
    if (chooser.show()) return false;
    filename = chooser.filename();
    return save_file();
}

bool editor_window::save_if_needed()
{
    if (!changed) return true;
    int rc = fl_choice("The document has unsaved changes.", "Cancel", "Save", "Discard");
    if (rc == 0) return false;
    if (rc == 1) return save_file();
    return true;
}

void editor_window::update_title()
{
    std::string title = "Latea";
    if (!filename.empty()) title += " - " + filename;
    if (changed) title += " *";
    ui->main_window->copy_label(title.c_str());
}

void editor_window::update_status(const char *text)
{
    ui->status_box->copy_label(text ? text : "");
    ui->status_box->redraw();
}

void editor_window::update_ai_status(const char *text)
{
    ui->ai_status_box->copy_label(text ? text : "");
    ui->ai_status_box->redraw();
}

void editor_window::show_find_dialog()
{
    ui->find_window->show();
    ui->find_text_input->take_focus();
}

void editor_window::show_preferences_dialog()
{
    sync_ui_to_prefs();
    ui->prefs_window->show();
}

void editor_window::choose_font()
{
    font_dialog_accepted = 0;
    font_dialog_selected_name = prefs.text_font_name;
    font_dialog_selected_size = prefs.text_size;
    font_dialog_selected_color = fl_rgb_color((uchar)prefs.text_r, (uchar)prefs.text_g, (uchar)prefs.text_b);

    ui->font_browser->clear();
    font_dialog_fonts.clear();
    font_dialog_sizes.clear();

    int font_count = Fl::set_fonts("*");
    for (int i = FL_FREE_FONT; i < font_count; i++) {
        int attr = 0;
        const char *name = Fl::get_font_name((Fl_Font)i, &attr);
        if (!name || !name[0]) continue;
        font_dialog_fonts.push_back((Fl_Font)i);
        font_dialog_sizes.push_back(std::vector<int>());
        ui->font_browser->add(name);
    }

    if (font_dialog_fonts.empty()) return;

    int selected_index = 0;
    for (int i = 0; i < ui->font_browser->size(); i++) {
        const char *entry = ui->font_browser->text(i + 1);
        if (entry && font_dialog_selected_name == entry) {
            selected_index = i;
            break;
        }
    }
    ui->font_browser->value(selected_index + 1);
    populate_size_browser(this, selected_index);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", font_dialog_selected_size > 0 ? font_dialog_selected_size : 16);
    ui->font_size_input->value(buf);
    for (int i = 0; i < ui->font_size_browser->size(); i++) {
        if (atoi(ui->font_size_browser->text(i + 1)) == font_dialog_selected_size) {
            ui->font_size_browser->value(i + 1);
            break;
        }
    }

    update_font_preview(this);

    ui->font_window->show();
    while (ui->font_window->shown()) Fl::wait();

    if (!font_dialog_accepted) return;
    prefs.text_font_name = font_dialog_selected_name;
    prefs.text_size = font_dialog_selected_size;
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(font_dialog_selected_color, r, g, b);
    prefs.text_r = r;
    prefs.text_g = g;
    prefs.text_b = b;
    prefs.save();
    apply_view_preferences();
    update_status("Font updated");
}

void editor_window::font_browser_changed()
{
    int index = ui->font_browser->value() - 1;
    if (index < 0 || index >= (int)font_dialog_fonts.size()) return;
    populate_size_browser(this, index);
    update_font_preview(this);
}

void editor_window::font_size_browser_changed()
{
    int font_index = ui->font_browser->value() - 1;
    int size_index = ui->font_size_browser->value() - 1;
    if (font_index < 0 || size_index < 0) return;
    if (font_index >= (int)font_dialog_sizes.size()) return;
    if (size_index >= (int)font_dialog_sizes[font_index].size()) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", font_dialog_sizes[font_index][size_index]);
    ui->font_size_input->value(buf);
    update_font_preview(this);
}

void editor_window::font_size_input_changed()
{
    update_font_preview(this);
}

void editor_window::choose_font_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(font_dialog_selected_color, r, g, b);
    if (!fl_color_chooser("Choose Font Color", r, g, b)) return;
    font_dialog_selected_color = fl_rgb_color(r, g, b);
    update_font_preview(this);
}

void editor_window::accept_font_dialog()
{
    int font_index = ui->font_browser->value() - 1;
    if (font_index < 0 || font_index >= (int)font_dialog_fonts.size()) return;

    int attr = 0;
    const char *name = Fl::get_font_name(font_dialog_fonts[font_index], &attr);
    font_dialog_selected_name = name ? name : "";
    font_dialog_selected_size = atoi(ui->font_size_input->value());
    if (font_dialog_selected_size <= 0) font_dialog_selected_size = 16;
    font_dialog_accepted = 1;
    ui->font_window->hide();
}

void editor_window::choose_background_color()
{
    uchar r = (uchar)prefs.bg_r;
    uchar g = (uchar)prefs.bg_g;
    uchar b = (uchar)prefs.bg_b;
    if (!fl_color_chooser("Choose Background Color", r, g, b)) return;
    prefs.bg_r = r;
    prefs.bg_g = g;
    prefs.bg_b = b;
    prefs.save();
    apply_view_preferences();
    update_status("Background color updated");
}

void editor_window::browse_dictionary_path()
{
    Fl_Native_File_Chooser chooser;
    chooser.title("Choose Vocabulary File");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    if (ui->dictionary_path_input->value()[0]) chooser.preset_file(ui->dictionary_path_input->value());
    if (chooser.show() == 0) ui->dictionary_path_input->value(chooser.filename());
}

void editor_window::find_next()
{
    const char *needle = ui->find_text_input->value();
    if (!needle || !needle[0]) return;

    int pos = editor->insert_position();
    int found = textbuf->search_forward(pos, needle, &pos);
    if (!found) found = textbuf->search_forward(0, needle, &pos);
    if (!found) {
        update_status("Text not found");
        return;
    }

    int end = pos + (int)strlen(needle);
    textbuf->select(pos, end);
    editor->insert_position(end);
    editor->show_insert_position();
    update_status("Match found");
}

void editor_window::replace_next()
{
    const char *needle = ui->find_text_input->value();
    const char *replacement = ui->replace_text_input->value();
    if (!needle || !needle[0]) return;

    int pos = editor->insert_position();
    int found = textbuf->search_forward(pos, needle, &pos);
    if (!found) found = textbuf->search_forward(0, needle, &pos);
    if (!found) {
        update_status("Text not found");
        return;
    }

    textbuf->replace(pos, pos + (int)strlen(needle), replacement);
    editor->insert_position(pos + (int)strlen(replacement));
    editor->show_insert_position();
    update_status("Replaced next match");
}

void editor_window::replace_all()
{
    const char *needle = ui->find_text_input->value();
    const char *replacement = ui->replace_text_input->value();
    if (!needle || !needle[0]) return;

    int count = 0;
    int pos = 0;
    while (textbuf->search_forward(pos, needle, &pos)) {
        textbuf->replace(pos, pos + (int)strlen(needle), replacement);
        pos += (int)strlen(replacement);
        count++;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Replaced %d occurrence(s)", count);
    update_status(msg);
}

void editor_window::apply_view_preferences()
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

    Fl_Color text_color = fl_rgb_color((uchar)prefs.text_r, (uchar)prefs.text_g, (uchar)prefs.text_b);
    Fl_Color bg = fl_rgb_color((uchar)prefs.bg_r, (uchar)prefs.bg_g, (uchar)prefs.bg_b);
    editor->color(bg);
    editor->textcolor(text_color);
    editor->cursor_color(text_color);
    editor->selection_color(fl_rgb_color((uchar)((prefs.bg_r + 80) % 256), (uchar)((prefs.bg_g + 100) % 256), (uchar)((prefs.bg_b + 160) % 256)));
    editor->linenumber_bgcolor(fl_rgb_color((uchar)(prefs.bg_r > 20 ? prefs.bg_r - 20 : 0),
        (uchar)(prefs.bg_g > 20 ? prefs.bg_g - 20 : 0), (uchar)(prefs.bg_b > 20 ? prefs.bg_b - 20 : 0)));
    editor->linenumber_fgcolor(text_color);

    set_word_wrap(prefs.word_wrap);
    set_line_numbers(prefs.line_numbers);
    sync_ui_to_prefs();
    editor->redraw();
}

void editor_window::apply_preferences()
{
    prefs.save();
    sync_ui_to_prefs();
    apply_view_preferences();
    autocomplete.on_preferences_changed();
    update_status("Config saved");
}

void editor_window::set_autocomplete_mode(int mode)
{
    prefs.autocomplete_mode = mode;
    apply_preferences();
}

void editor_window::set_word_wrap(int enabled)
{
    prefs.word_wrap = enabled;
    if (enabled) editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    else editor->wrap_mode(Fl_Text_Display::WRAP_NONE, 0);
}

void editor_window::set_line_numbers(int enabled)
{
    prefs.line_numbers = enabled;
    if (enabled) editor->linenumber_width(line_number_width(textbuf, current_font, current_size));
    else editor->linenumber_width(0);
}

void editor_window::set_suggestion(const std::string &text, int anchor_pos, int request_id)
{
    std::string clean = sanitize_suggestion(text, prefs.max_suggestion_chars);
    if (clean.empty()) {
        clear_suggestion();
        return;
    }
    suggestion.text = clean;
    suggestion.anchor_pos = anchor_pos;
    suggestion.request_id = request_id;
    suggestion.visible = 1;
    editor->redraw();
}

void editor_window::clear_suggestion()
{
    if (!suggestion.visible) return;
    suggestion.clear();
    editor->redraw();
}

void editor_window::accept_suggestion_full()
{
    if (!suggestion.visible) return;
    int pos = editor->insert_position();
    std::string insert = suggestion.text;
    clear_suggestion();
    textbuf->insert(pos, insert.c_str());
    editor->insert_position(pos + (int)insert.size());
    editor->show_insert_position();
    autocomplete.schedule();
}

void editor_window::accept_suggestion_word()
{
    if (!suggestion.visible) return;
    std::string part = suggestion.text;
    size_t split = part.find_first_of(" \t");
    if (split != std::string::npos) part.erase(split);
    int pos = editor->insert_position();
    clear_suggestion();
    textbuf->insert(pos, part.c_str());
    editor->insert_position(pos + (int)part.size());
    editor->show_insert_position();
    autocomplete.schedule();
}

void editor_window::record_buffer_change(int pos, int inserted, int deleted, const char *deleted_text)
{
    (void)deleted;
    if (suppress_history || history.replaying) return;

    char *inserted_text = inserted > 0 ? textbuf->text_range(pos, pos + inserted) : 0;
    edit_action action;
    action.pos = pos;
    action.removed = deleted_text ? deleted_text : "";
    action.inserted = inserted_text ? inserted_text : "";
    action.cursor_before = pos;
    action.cursor_after = pos + inserted;
    free(inserted_text);

    history.record(action);
    set_changed(1);
    if (prefs.line_numbers) set_line_numbers(1);
}

void editor_window::set_changed(int value)
{
    changed = value;
    update_title();
}

void editor_window::close_main_window()
{
    if (!save_if_needed()) return;
    ui->main_window->hide();
}

void editor_window::text_modified_cb(int pos, int nInserted, int nDeleted,
    int nRestyled, const char *deletedText, void *cbArg)
{
    (void)nRestyled;
    editor_window *window_ptr = (editor_window *)cbArg;
    window_ptr->record_buffer_change(pos, nInserted, nDeleted, deletedText);
    window_ptr->autocomplete.on_text_changed();
}
