#pragma once

#include <string>
#include <stdint.h>

#define CONFIG_PATH ".latea"
#define CONFIG_FILE "latea.cfg"
#define CONFIG_MODE 0775

enum {
    AUTOCOMPLETE_DISABLED = 0,
    AUTOCOMPLETE_DICTIONARY_FILE = 1,
    AUTOCOMPLETE_CURRENT_FILE = 2,
    AUTOCOMPLETE_AI = 3,
    AUTOCOMPLETE_EMBEDDED_AI = 4
};

enum {
    AI_ENDPOINT_COMPLETION = 0,
    AI_ENDPOINT_PREFER_INFILL = 1
};

struct app_prefs
{
    std::string dict_path;
    int autocomp_mode;
    bool cont_autocomp;
    int max_suggestion_chars;
    std::string model_path;
    std::string text_font_name;
    int text_size;
    uint32_t text_color;
    uint32_t ghost_color;
    uint32_t bg_color;
    uint32_t sel_color;
    uint32_t line_bgcol;
    uint32_t line_fgcol;
    bool word_wrap;
    bool line_numbers;
    std::string ai_host;
    int ai_port;
    int ai_endpoint_mode;
    std::string ai_system_prompt;
    int ai_prefix_chars;
    int ai_suffix_chars;
    int ai_delay_ms;
    int ai_timeout_ms;
    int ai_context_length;
    double ai_temperature;
    double ai_top_p;
    int ai_top_k;
    bool ai_cache_prompt;
    int ai_slot_id;

    void set_defaults();
    void load();
    void save() const;
};
