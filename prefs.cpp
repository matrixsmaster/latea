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
    const char* h = getenv("HOME");
    if (!h || !h[0]) return CONFIG_PATH;
    return string(h) + "/" + CONFIG_PATH;
}

void app_prefs::set_defaults()
{
    dict_path.clear();
    autocomp_mode = AUTOCOMPLETE_DISABLED;
    cont_autocomp = false;
    max_suggestion_chars = 80;
    model_path.clear();
    text_font_name.clear();
    text_size = 16;
    text_color = RGB_U32(0, 0, 0);
    ghost_color = RGB_U32(99, 99, 99);
    bg_color = RGB_U32(255, 255, 255);
    sel_color = RGB_U32(184, 208, 255);
    line_bgcol = RGB_U32(235, 235, 235);
    line_fgcol = RGB_U32(0, 0, 0);
    word_wrap = false;
    line_numbers = false;
    ai_host = "127.0.0.1";
    ai_port = 8080;
    ai_endpoint_mode = AI_ENDPOINT_COMPLETION;
    ai_prefix_chars = 3000;
    ai_suffix_chars = 800;
    ai_delay_ms = 180;
    ai_timeout_ms = 10000;
    ai_context_length = 4096;
    ai_temperature = 0.2;
    ai_top_p = 0.9;
    ai_top_k = 40;
    ai_cache_prompt = true;
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

        if (key == "dict_path") dict_path = value;
        else if (key == "autocomp_mode") autocomp_mode = atoi(value.c_str());
        else if (key == "cont_autocomp") cont_autocomp = atoi(value.c_str()) != 0;
        else if (key == "max_suggestion_chars") max_suggestion_chars = atoi(value.c_str());
        else if (key == "model_path") model_path = value;
        else if (key == "text_font_name") text_font_name = json_unescape(value);
        else if (key == "text_size") text_size = atoi(value.c_str());
        else if (key == "text_color") text_color = strtoul(value.c_str(), NULL, 0);
        else if (key == "ghost_color") ghost_color = strtoul(value.c_str(), NULL, 0);
        else if (key == "bg_color") bg_color = strtoul(value.c_str(), NULL, 0);
        else if (key == "sel_color") sel_color = strtoul(value.c_str(), NULL, 0);
        else if (key == "line_bgcol") line_bgcol = strtoul(value.c_str(), NULL, 0);
        else if (key == "line_fgcol") line_fgcol = strtoul(value.c_str(), NULL, 0);
        else if (key == "word_wrap") word_wrap = atoi(value.c_str()) != 0;
        else if (key == "line_numbers") line_numbers = atoi(value.c_str()) != 0;
        else if (key == "ai_host") ai_host = value;
        else if (key == "ai_port") ai_port = atoi(value.c_str());
        else if (key == "ai_endpoint_mode") ai_endpoint_mode = atoi(value.c_str());
        else if (key == "ai_system_prompt") ai_system_prompt = json_unescape(value);
        else if (key == "ai_prefix_chars") ai_prefix_chars = atoi(value.c_str());
        else if (key == "ai_suffix_chars") ai_suffix_chars = atoi(value.c_str());
        else if (key == "ai_delay_ms") ai_delay_ms = atoi(value.c_str());
        else if (key == "ai_timeout_ms") ai_timeout_ms = atoi(value.c_str());
        else if (key == "ai_context_length") ai_context_length = atoi(value.c_str());
        else if (key == "ai_temperature") ai_temperature = atof(value.c_str());
        else if (key == "ai_top_p") ai_top_p = atof(value.c_str());
        else if (key == "ai_top_k") ai_top_k = atoi(value.c_str());
        else if (key == "ai_cache_prompt") ai_cache_prompt = atoi(value.c_str()) != 0;
        else if (key == "ai_slot_id") ai_slot_id = atoi(value.c_str());
    }
}

void app_prefs::save() const
{
    mkdir(config_dir().c_str(),CONFIG_MODE);
    ofstream file(config_dir() + "/" + CONFIG_FILE);

    file << "dict_path=" << dict_path << "\n";
    file << "autocomp_mode=" << autocomp_mode << "\n";
    file << "cont_autocomp=" << cont_autocomp << "\n";
    file << "max_suggestion_chars=" << max_suggestion_chars << "\n";
    file << "model_path=" << model_path << "\n";
    file << "text_font_name=" << json_escape(text_font_name) << "\n";
    file << "text_size=" << text_size << "\n";
    file << "text_color=0x" << std::hex << text_color << std::dec << "\n";
    file << "ghost_color=0x" << std::hex << ghost_color << std::dec << "\n";
    file << "bg_color=0x" << std::hex << bg_color << std::dec << "\n";
    file << "sel_color=0x" << std::hex << sel_color << std::dec << "\n";
    file << "line_bgcol=0x" << std::hex << line_bgcol << std::dec << "\n";
    file << "line_fgcol=0x" << std::hex << line_fgcol << std::dec << "\n";
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
    file << "ai_context_length=" << ai_context_length << "\n";
    file << "ai_temperature=" << ai_temperature << "\n";
    file << "ai_top_p=" << ai_top_p << "\n";
    file << "ai_top_k=" << ai_top_k << "\n";
    file << "ai_cache_prompt=" << ai_cache_prompt << "\n";
    file << "ai_slot_id=" << ai_slot_id << "\n";
}
