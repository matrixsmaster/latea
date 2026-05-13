#ifndef LATEA_PREFS_H
#define LATEA_PREFS_H

#include <string>

#define CONFIG_PATH ".latea"
#define CONFIG_FILE "latea.cfg"
#define CONFIG_MODE 0755

enum {
    AUTOCOMPLETE_DISABLED = 0,
    AUTOCOMPLETE_DICTIONARY_FILE = 1,
    AUTOCOMPLETE_CURRENT_FILE = 2,
    AUTOCOMPLETE_AI = 3
};

enum {
    AI_ENDPOINT_COMPLETION = 0,
    AI_ENDPOINT_PREFER_INFILL = 1
};

struct app_prefs
{
    std::string dictionary_path;
    int autocomplete_mode;
    int continuous_autocomplete;
    int max_suggestion_chars;
    std::string text_font_name;
    int text_size;
    int text_r;
    int text_g;
    int text_b;
    int bg_r;
    int bg_g;
    int bg_b;
    int word_wrap;
    int line_numbers;
    std::string ai_host;
    int ai_port;
    int ai_endpoint_mode;
    std::string ai_system_prompt;
    int ai_prefix_chars;
    int ai_suffix_chars;
    int ai_delay_ms;
    int ai_timeout_ms;
    double ai_temperature;
    double ai_top_p;
    int ai_cache_prompt;
    int ai_slot_id;

    void set_defaults();
    void load();
    void save() const;
};

#endif
