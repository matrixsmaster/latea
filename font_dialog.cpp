#include <cstdio>
#include <cstdlib>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Color_Chooser.H>
#include "common.h"
#include "editor.h"
#include "font_dialog.h"
#include "latea_ui.h"

font_dialog* g_font_dlg = NULL;

static void populate_size_browser(int index)
{
    int* font_sizes = NULL;
    int count;
    int defaults[] = FONT_SIZE_DEFAULTS;

    g_wnd->ui->font_size_browser->clear();
    g_font_dlg->sizes[index].clear();

    count = Fl::get_font_sizes(g_font_dlg->fonts[index], font_sizes);
    if (count > 0 && !(count == 1 && font_sizes[0] == 0)) {
        for (int i = 0; i < count; i++) {
            char buf[32];
            if (font_sizes[i] <= 0) continue;
            snprintf(buf, sizeof(buf), "%d", font_sizes[i]);
            g_wnd->ui->font_size_browser->add(buf);
            g_font_dlg->sizes[index].push_back(font_sizes[i]);
        }
    }

    if (!g_font_dlg->sizes[index].empty()) return;
    for (unsigned int i = 0; i < NUMITEMS(defaults); i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", defaults[i]);
        g_wnd->ui->font_size_browser->add(buf);
        g_font_dlg->sizes[index].push_back(defaults[i]);
    }
}

font_dialog::font_dialog()
{
    selected_size = 16;
    selected_text_color = FL_BLACK;
    selected_ghost_color = FL_DARK3;
    selected_bg_color = FL_WHITE;
    selected_selection_color = fl_rgb_color(184, 208, 255);
    selected_linenumber_bg_color = fl_rgb_color(235, 235, 235);
    selected_linenumber_fg_color = FL_BLACK;
}

void font_dialog::open()
{
    int font_count;
    int selected_index = 0;
    char buf[32];

    selected_name = g_wnd->prefs.text_font_name;
    selected_size = g_wnd->prefs.text_size;
    selected_text_color = fl_rgb_color(RGB_R(g_wnd->prefs.text_color), RGB_G(g_wnd->prefs.text_color), RGB_B(g_wnd->prefs.text_color));
    selected_ghost_color = fl_rgb_color(RGB_R(g_wnd->prefs.ghost_color), RGB_G(g_wnd->prefs.ghost_color), RGB_B(g_wnd->prefs.ghost_color));
    selected_bg_color = fl_rgb_color(RGB_R(g_wnd->prefs.bg_color), RGB_G(g_wnd->prefs.bg_color), RGB_B(g_wnd->prefs.bg_color));
    selected_selection_color = fl_rgb_color(RGB_R(g_wnd->prefs.sel_color), RGB_G(g_wnd->prefs.sel_color), RGB_B(g_wnd->prefs.sel_color));
    selected_linenumber_bg_color = fl_rgb_color(RGB_R(g_wnd->prefs.line_bgcol), RGB_G(g_wnd->prefs.line_bgcol), RGB_B(g_wnd->prefs.line_bgcol));
    selected_linenumber_fg_color = fl_rgb_color(RGB_R(g_wnd->prefs.line_fgcol), RGB_G(g_wnd->prefs.line_fgcol), RGB_B(g_wnd->prefs.line_fgcol));

    g_wnd->ui->font_browser->clear();
    fonts.clear();
    sizes.clear();

    font_count = Fl::set_fonts("*");
    for (int i = FL_FREE_FONT; i < font_count; i++) {
        int attr = 0;
        const char* name = Fl::get_font_name((Fl_Font)i, &attr);
        if (!name || !name[0]) continue;
        fonts.push_back((Fl_Font)i);
        sizes.push_back(std::vector<int>());
        g_wnd->ui->font_browser->add(name);
    }
    if (fonts.empty()) return;

    for (int i = 0; i < g_wnd->ui->font_browser->size(); i++) {
        const char* entry = g_wnd->ui->font_browser->text(i + 1);
        if (entry && selected_name == entry) {
            selected_index = i;
            break;
        }
    }

    g_wnd->ui->font_browser->value(selected_index + 1);
    populate_size_browser(selected_index);

    snprintf(buf, sizeof(buf), "%d", selected_size > 0 ? selected_size : 16);
    g_wnd->ui->font_size_input->value(buf);
    for (int i = 0; i < g_wnd->ui->font_size_browser->size(); i++) {
        if (atoi(g_wnd->ui->font_size_browser->text(i + 1)) != selected_size) continue;
        g_wnd->ui->font_size_browser->value(i + 1);
        break;
    }

    refresh_preview();
    g_wnd->ui->font_window->show();
}

void font_dialog::refresh_preview()
{
    int font_index = g_wnd->ui->font_browser->value() - 1;
    int size = atoi(g_wnd->ui->font_size_input->value());

    if (font_index < 0 || font_index >= (int)fonts.size()) return;
    if (size <= 0) size = 16;

    g_wnd->ui->font_preview->copy_label("The quick brown fox");
    g_wnd->ui->font_preview->labelfont(fonts[font_index]);
    g_wnd->ui->font_preview->labelsize(size);
    g_wnd->ui->font_preview->labelcolor(selected_text_color);
    g_wnd->ui->font_preview->color(selected_bg_color);
    g_wnd->ui->font_preview->selection_color(selected_selection_color);
    g_wnd->ui->font_preview->redraw();

    g_wnd->ui->ghost_preview->copy_label("jumps over the lazy dog");
    g_wnd->ui->ghost_preview->labelfont(fonts[font_index]);
    g_wnd->ui->ghost_preview->labelsize(size);
    g_wnd->ui->ghost_preview->labelcolor(selected_ghost_color);
    g_wnd->ui->ghost_preview->color(selected_bg_color);
    g_wnd->ui->ghost_preview->redraw();
}

void font_dialog::font_browser_changed()
{
    int index = g_wnd->ui->font_browser->value() - 1;
    if (index < 0 || index >= (int)fonts.size()) return;
    populate_size_browser(index);
    refresh_preview();
}

void font_dialog::font_size_browser_changed()
{
    int font_index = g_wnd->ui->font_browser->value() - 1;
    int size_index = g_wnd->ui->font_size_browser->value() - 1;
    char buf[32];

    if (font_index < 0 || size_index < 0) return;
    if (font_index >= (int)sizes.size()) return;
    if (size_index >= (int)sizes[font_index].size()) return;

    snprintf(buf, sizeof(buf), "%d", sizes[font_index][size_index]);
    g_wnd->ui->font_size_input->value(buf);
    refresh_preview();
}

void font_dialog::choose_text_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_text_color, r, g, b);
    if (!fl_color_chooser("Choose Text Color", r, g, b)) return;
    selected_text_color = fl_rgb_color(r, g, b);
    refresh_preview();
}

void font_dialog::choose_ghost_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_ghost_color, r, g, b);
    if (!fl_color_chooser("Choose Ghost Color", r, g, b)) return;
    selected_ghost_color = fl_rgb_color(r, g, b);
    refresh_preview();
}

void font_dialog::choose_background_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_bg_color, r, g, b);
    if (!fl_color_chooser("Choose Background Color", r, g, b)) return;
    selected_bg_color = fl_rgb_color(r, g, b);
    refresh_preview();
}

void font_dialog::choose_selection_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_selection_color, r, g, b);
    if (!fl_color_chooser("Choose Selection Color", r, g, b)) return;
    selected_selection_color = fl_rgb_color(r, g, b);
    refresh_preview();
}

void font_dialog::choose_linenumber_bg_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_linenumber_bg_color, r, g, b);
    if (!fl_color_chooser("Choose Line Number Background", r, g, b)) return;
    selected_linenumber_bg_color = fl_rgb_color(r, g, b);
}

void font_dialog::choose_linenumber_fg_color()
{
    uchar r = 0, g = 0, b = 0;
    Fl::get_color(selected_linenumber_fg_color, r, g, b);
    if (!fl_color_chooser("Choose Line Number Text", r, g, b)) return;
    selected_linenumber_fg_color = fl_rgb_color(r, g, b);
}

void font_dialog::accept()
{
    int font_index = g_wnd->ui->font_browser->value() - 1;
    int attr = 0;
    const char* name;
    uchar r = 0, g = 0, b = 0;

    if (font_index < 0 || font_index >= (int)fonts.size()) return;

    name = Fl::get_font_name(fonts[font_index], &attr);
    selected_name = name ? name : "";
    selected_size = atoi(g_wnd->ui->font_size_input->value());
    if (selected_size <= 0) selected_size = 16;

    g_wnd->prefs.text_font_name = selected_name;
    g_wnd->prefs.text_size = selected_size;

    Fl::get_color(selected_text_color, r, g, b);
    g_wnd->prefs.text_color = RGB_U32(r, g, b);
    Fl::get_color(selected_ghost_color, r, g, b);
    g_wnd->prefs.ghost_color = RGB_U32(r, g, b);
    Fl::get_color(selected_bg_color, r, g, b);
    g_wnd->prefs.bg_color = RGB_U32(r, g, b);
    Fl::get_color(selected_selection_color, r, g, b);
    g_wnd->prefs.sel_color = RGB_U32(r, g, b);
    Fl::get_color(selected_linenumber_bg_color, r, g, b);
    g_wnd->prefs.line_bgcol = RGB_U32(r, g, b);
    Fl::get_color(selected_linenumber_fg_color, r, g, b);
    g_wnd->prefs.line_fgcol = RGB_U32(r, g, b);

    g_wnd->ui->font_window->hide();
    g_wnd->apply_preferences();
    g_wnd->update_status("Appearance updated");
}
