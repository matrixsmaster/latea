#pragma once

#include <string>
#include <stdint.h>
#include <map>
#include <vector>

#define CONFIG_PATH ".latea"
#define CONFIG_FILE "latea.cfg"
#define CONFIG_MODE 0775
#define DEF_TEXT_FONT FL_COURIER
#define DEF_TEXT_SIZE 16
#define PREFS_MAIN "Latea"

enum {
    AUTOCOMPLETE_DISABLED = 0,
    AUTOCOMPLETE_DICTIONARY_FILE = 1,
    AUTOCOMPLETE_CURRENT_FILE = 2,
    AUTOCOMPLETE_AI = 3,
    AUTOCOMPLETE_EMBEDDED_AI = 4,
    AUTOCOMPLETE_MARKOV = 5,
};

struct app_prefs
{
    int win_w, win_h;
    std::string last_preset;
    std::string dict_path;
    int autocomp_mode;
    bool cont_autocomp;
    int max_suggestion;
    std::string model_path;
    std::string text_font_name;
    int text_size;
    uint32_t text_color;
    uint32_t ghost_color;
    uint32_t bg_color;
    uint32_t sel_color;
    bool auto_indent;
    int tab_spaces;
    bool word_wrap;
    bool line_numbers;
    int ac_delay_ms;
    std::string ai_launch_path;
    std::string ai_host;
    int ai_port;
    bool ai_infill;
    std::string ai_system_prompt;
    int ai_prefix_chars;
    int ai_suffix_chars;
    int ai_timeout_ms;
    int ai_context_length;
    double ai_temperature;
    double ai_top_p;
    int ai_top_k;
    bool ai_cache_prompt;
    int ai_slot_id;
    bool ai_tq;
    bool save_backup;
    bool stop_punct;

    std::map<std::string, std::string> sets;

    void set_defaults();
    void load();
    void save();
    void load_item(std::string key, std::string val);
    std::string save_all() const;
    void use_preset(std::string name);
    void store_preset(std::string name);
    void del_preset(std::string name);
};
