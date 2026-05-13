#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include "common.h"
#include "prefs.h"

using namespace std;

static string config_dir()
{
    const char *h = getenv("HOME");
    if (!h || !h[0]) return CONFIG_PATH;
    return string(h) + "/" + CONFIG_PATH;
}

void app_prefs::set_defaults()
{
    dictionary_path.clear();
    autocomplete_mode = AUTOCOMPLETE_DISABLED;
    continuous_autocomplete = 0;
    max_suggestion_chars = 80;
    text_font_name.clear();
    text_size = 16;
    text_r = 0;
    text_g = 0;
    text_b = 0;
    bg_r = 255;
    bg_g = 255;
    bg_b = 255;
    word_wrap = 0;
    line_numbers = 0;
    ai_host = "127.0.0.1";
    ai_port = 8080;
    ai_endpoint_mode = AI_ENDPOINT_COMPLETION;
    ai_prefix_chars = 3000;
    ai_suffix_chars = 800;
    ai_delay_ms = 180;
    ai_timeout_ms = 10000;
    ai_temperature = 0.2;
    ai_top_p = 0.9;
    ai_cache_prompt = 1;
    ai_slot_id = 0;
}

void app_prefs::load()
{
    set_defaults();
    ifstream file(config_dir() + "/" + CONFIG_FILE);
    string line;

    while (getline(file, line)) {
        line = trim_newlines(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string value = line.substr(eq + 1);

        if (key == "dictionary_path") dictionary_path = value;
        else if (key == "autocomplete_mode") autocomplete_mode = atoi(value.c_str());
        else if (key == "continuous_autocomplete") continuous_autocomplete = atoi(value.c_str());
        else if (key == "max_suggestion_chars") max_suggestion_chars = atoi(value.c_str());
        else if (key == "text_font_name") text_font_name = json_unescape(value);
        else if (key == "text_size") text_size = atoi(value.c_str());
        else if (key == "text_r") text_r = atoi(value.c_str());
        else if (key == "text_g") text_g = atoi(value.c_str());
        else if (key == "text_b") text_b = atoi(value.c_str());
        else if (key == "bg_r") bg_r = atoi(value.c_str());
        else if (key == "bg_g") bg_g = atoi(value.c_str());
        else if (key == "bg_b") bg_b = atoi(value.c_str());
        else if (key == "word_wrap") word_wrap = atoi(value.c_str());
        else if (key == "line_numbers") line_numbers = atoi(value.c_str());
        else if (key == "ai_host") ai_host = value;
        else if (key == "ai_port") ai_port = atoi(value.c_str());
        else if (key == "ai_endpoint_mode") ai_endpoint_mode = atoi(value.c_str());
        else if (key == "ai_system_prompt") ai_system_prompt = json_unescape(value);
        else if (key == "ai_prefix_chars") ai_prefix_chars = atoi(value.c_str());
        else if (key == "ai_suffix_chars") ai_suffix_chars = atoi(value.c_str());
        else if (key == "ai_delay_ms") ai_delay_ms = atoi(value.c_str());
        else if (key == "ai_timeout_ms") ai_timeout_ms = atoi(value.c_str());
        else if (key == "ai_temperature") ai_temperature = atof(value.c_str());
        else if (key == "ai_top_p") ai_top_p = atof(value.c_str());
        else if (key == "ai_cache_prompt") ai_cache_prompt = atoi(value.c_str());
        else if (key == "ai_slot_id") ai_slot_id = atoi(value.c_str());
    }
}

void app_prefs::save() const
{
    mkdir(config_dir().c_str(),CONFIG_MODE);
    ofstream file(config_dir() + "/" + CONFIG_FILE);
    file << "dictionary_path=" << dictionary_path << "\n";
    file << "autocomplete_mode=" << autocomplete_mode << "\n";
    file << "continuous_autocomplete=" << continuous_autocomplete << "\n";
    file << "max_suggestion_chars=" << max_suggestion_chars << "\n";
    file << "text_font_name=" << json_escape(text_font_name) << "\n";
    file << "text_size=" << text_size << "\n";
    file << "text_r=" << text_r << "\n";
    file << "text_g=" << text_g << "\n";
    file << "text_b=" << text_b << "\n";
    file << "bg_r=" << bg_r << "\n";
    file << "bg_g=" << bg_g << "\n";
    file << "bg_b=" << bg_b << "\n";
    file << "word_wrap=" << word_wrap << "\n";
    file << "line_numbers=" << line_numbers << "\n";
    file << "ai_host=" << ai_host << "\n";
    file << "ai_port=" << ai_port << "\n";
    file << "ai_endpoint_mode=" << ai_endpoint_mode << "\n";
    file << "ai_system_prompt=" << json_escape(ai_system_prompt) << "\n";
    file << "ai_prefix_chars=" << ai_prefix_chars << "\n";
    file << "ai_suffix_chars=" << ai_suffix_chars << "\n";
    file << "ai_delay_ms=" << ai_delay_ms << "\n";
    file << "ai_timeout_ms=" << ai_timeout_ms << "\n";
    file << "ai_temperature=" << ai_temperature << "\n";
    file << "ai_top_p=" << ai_top_p << "\n";
    file << "ai_cache_prompt=" << ai_cache_prompt << "\n";
    file << "ai_slot_id=" << ai_slot_id << "\n";
}
