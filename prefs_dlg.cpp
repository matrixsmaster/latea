#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <FL/fl_ask.H>
#include "editor.h"
#include "prefs_dlg.h"
#include "latea_ui.h"

using namespace std;

prefs_dialog* g_prefs_dlg = NULL;

static void refill_presets()
{
    g_wnd->ui->prefs_preset->clear();
    for (map<string, string>::const_iterator it = g_prefs_dlg->prefs.sets.begin(); it != g_prefs_dlg->prefs.sets.end(); ++it) {
        if (it->first == PREFS_GLOBAL) continue;
        g_wnd->ui->prefs_preset->add(it->first.c_str());
    }
}

void prefs_dialog::sync_ui()
{
    app_prefs &p = prefs;
    g_wnd->ui->dictionary_path_input->value(p.dict_path.c_str());

    switch (p.autocomp_mode) {
    case AUTOCOMPLETE_DISABLED:
        g_wnd->ui->autocmp_dis->set();
        break;
    case AUTOCOMPLETE_DICTIONARY_FILE:
        g_wnd->ui->autocmp_dict->set();
        break;
    case AUTOCOMPLETE_CURRENT_FILE:
        g_wnd->ui->autocmp_cur->set();
        break;
    case AUTOCOMPLETE_AI:
        g_wnd->ui->autocmp_ai->set();
        break;
    case AUTOCOMPLETE_EMBEDDED_AI:
        g_wnd->ui->autocmp_embedded->set();
        break;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d", p.max_suggestion_chars);
    g_wnd->ui->max_chars_input->value(buf);
    g_wnd->ui->model_path_input->value(p.model_path.c_str());
    g_wnd->ui->llama_path_input->value(p.ai_launch_path.c_str());
    g_wnd->ui->ai_host_input->value(p.ai_host.c_str());
    snprintf(buf, sizeof(buf), "%d", p.ai_port);
    g_wnd->ui->ai_port_input->value(buf);
    g_wnd->ui->endpoint_mode_choice->value(p.ai_endpoint_mode);
    snprintf(buf, sizeof(buf), "%d", p.ai_delay_ms);
    g_wnd->ui->ai_delay_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_timeout_ms);
    g_wnd->ui->ai_timeout_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_context_length);
    g_wnd->ui->ai_context_input->value(buf);
    snprintf(buf, sizeof(buf), "%.3g", p.ai_temperature);
    g_wnd->ui->ai_temperature_input->value(buf);
    snprintf(buf, sizeof(buf), "%.3g", p.ai_top_p);
    g_wnd->ui->ai_top_p_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_top_k);
    g_wnd->ui->ai_top_k_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_prefix_chars);
    g_wnd->ui->ai_prefix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_suffix_chars);
    g_wnd->ui->ai_suffix_input->value(buf);
    snprintf(buf, sizeof(buf), "%d", p.ai_slot_id);
    g_wnd->ui->ai_slot_input->value(buf);
    g_wnd->ui->ai_cache_check->value(p.ai_cache_prompt ? 1 : 0);
    g_wnd->ui->ai_tq_check->value(p.ai_tq ? 1 : 0);
    g_wnd->ui->system_prompt_input->value(p.ai_system_prompt.c_str());

    p.cont_autocomp ? g_wnd->ui->autocmp_cont->set() : g_wnd->ui->autocmp_cont->clear();
    p.word_wrap ? g_wnd->ui->view_wrdwrp->set() : g_wnd->ui->view_wrdwrp->clear();
    p.line_numbers ? g_wnd->ui->view_lines->set() : g_wnd->ui->view_lines->clear();
    g_wnd->ui->prefs_preset->value(p.last_preset.c_str());
}

void prefs_dialog::sync_from_ui()
{
    prefs.dict_path = g_wnd->ui->dictionary_path_input->value();
    prefs.max_suggestion_chars = atoi(g_wnd->ui->max_chars_input->value());
    prefs.model_path = g_wnd->ui->model_path_input->value();
    prefs.ai_launch_path = g_wnd->ui->llama_path_input->value();
    prefs.ai_host = g_wnd->ui->ai_host_input->value();
    prefs.ai_port = atoi(g_wnd->ui->ai_port_input->value());
    prefs.ai_endpoint_mode = g_wnd->ui->endpoint_mode_choice->value();
    prefs.ai_delay_ms = atoi(g_wnd->ui->ai_delay_input->value());
    prefs.ai_timeout_ms = atoi(g_wnd->ui->ai_timeout_input->value());
    prefs.ai_context_length = atoi(g_wnd->ui->ai_context_input->value());
    prefs.ai_temperature = atof(g_wnd->ui->ai_temperature_input->value());
    prefs.ai_top_p = atof(g_wnd->ui->ai_top_p_input->value());
    prefs.ai_top_k = atoi(g_wnd->ui->ai_top_k_input->value());
    prefs.ai_prefix_chars = atoi(g_wnd->ui->ai_prefix_input->value());
    prefs.ai_suffix_chars = atoi(g_wnd->ui->ai_suffix_input->value());
    prefs.ai_slot_id = atoi(g_wnd->ui->ai_slot_input->value());
    prefs.ai_cache_prompt = g_wnd->ui->ai_cache_check->value() != 0;
    prefs.ai_tq = g_wnd->ui->ai_tq_check->value() != 0;
    prefs.ai_system_prompt = g_wnd->ui->system_prompt_input->value();
}

void prefs_dialog::open()
{
    prefs = g_wnd->prefs;
    prefs.store_preset(prefs.last_preset);
    refill_presets();
    if (prefs.sets.find(prefs.last_preset) == prefs.sets.end()) prefs.last_preset = PREFS_MAIN;
    prefs.use_preset(prefs.last_preset);
    sync_ui();
    g_wnd->ui->prefs_window->show();
}

void prefs_dialog::preset_changed()
{
    const char* name = g_wnd->ui->prefs_preset->value();
    if (!name || !name[0]) {
        g_wnd->ui->prefs_preset->value(prefs.last_preset.c_str());
        return;
    }
    if (prefs.sets.find(name) == prefs.sets.end()) {
        g_wnd->ui->prefs_preset->value(prefs.last_preset.c_str());
        return;
    }
    sync_from_ui();
    prefs.store_preset(prefs.last_preset);
    prefs.last_preset = name;
    prefs.use_preset(prefs.last_preset);
    sync_ui();
}

void prefs_dialog::accept()
{
    sync_from_ui();
    prefs.store_preset(prefs.last_preset);
    g_wnd->prefs = prefs;
    g_wnd->ui->prefs_window->hide();
    g_wnd->apply_preferences();
}

void prefs_dialog::new_preset()
{
    sync_from_ui();
    prefs.store_preset(prefs.last_preset);
    const char* name = fl_input("Preset name:", "");
    if (!name || !name[0]) return;
    if (!strcmp(name, PREFS_GLOBAL)) {
        fl_alert("Preset name '%s' is reserved.", name);
        return;
    }
    if (prefs.sets.find(name) != prefs.sets.end()) {
        fl_alert("Preset '%s' already exists.", name);
        return;
    }
    prefs.last_preset = name;
    prefs.store_preset(prefs.last_preset);
    refill_presets();
    sync_ui();
}

void prefs_dialog::del_preset()
{
    const char* name = g_wnd->ui->prefs_preset->value();
    if (!name || !name[0] || !strcmp(name, PREFS_MAIN)) {
        fl_alert("The main preset cannot be deleted.");
        return;
    }
    if (fl_choice("Delete preset '%s'?", "Cancel", "Delete", NULL, name) != 1) return;
    prefs.del_preset(name);
    prefs.use_preset(prefs.last_preset);
    refill_presets();
    sync_ui();
}
