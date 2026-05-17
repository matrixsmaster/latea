#pragma once

#include "prefs.h"

struct prefs_dialog
{
    app_prefs prefs;

    void open();
    void sync_ui();
    void sync_from_ui();
    void preset_changed();
    void accept();
    void new_preset();
    void del_preset();
};

extern prefs_dialog* g_prefs_dlg;
