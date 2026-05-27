#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include "common.h"
#include "prefs.h"

using namespace std;

void app_prefs::set_defaults()
{
    win_w = 0;
    win_h = 0;
    last_preset = PREFS_MAIN;
    dict_path.clear();
    autocomp_mode = AUTOCOMPLETE_DISABLED;
    cont_autocomp = false;
    max_suggestion = 80;
    model_path.clear();
    text_font_name.clear();
    text_size = DEF_TEXT_SIZE;
    text_color = RGB_U32(0, 0, 0);
    ghost_color = RGB_U32(99, 99, 99);
    bg_color = RGB_U32(255, 255, 255);
    sel_color = RGB_U32(184, 208, 255);
    auto_indent = false;
    tab_spaces = 0;
    word_wrap = false;
    line_numbers = false;
    ac_delay_ms = 180;
    ai_launch_path.clear();
    ai_host = "127.0.0.1";
    ai_port = 8080;
    ai_infill = false;
    ai_system_prompt.clear();
    ai_prefix_chars = 3000;
    ai_suffix_chars = 800;
    ai_timeout_ms = 10000;
    ai_context_length = 4096;
    ai_temperature = 0.2;
    ai_top_p = 0.9;
    ai_top_k = 40;
    ai_cache_prompt = true;
    ai_slot_id = 0;
    ai_tq = false;
    save_backup = false;
    stop_punct = false;
}

static string config_dir()
{
    const char* h = getenv("HOME");
    if (!h || !h[0]) return CONFIG_PATH;
    return string(h) + "/" + CONFIG_PATH;
}

void app_prefs::load_item(string key, string val)
{
    if (key == "win_w") win_w = atoi(val.c_str());
    else if (key == "win_h") win_h = atoi(val.c_str());
    else if (key == "dict_path") dict_path = json_unescape(val);
    else if (key == "autocomp_mode") autocomp_mode = atoi(val.c_str());
    else if (key == "cont_autocomp") cont_autocomp = atoi(val.c_str()) != 0;
    else if (key == "max_suggestion") max_suggestion = atoi(val.c_str());
    else if (key == "model_path") model_path = json_unescape(val);
    else if (key == "text_font_name") text_font_name = json_unescape(val);
    else if (key == "text_size") text_size = atoi(val.c_str());
    else if (key == "text_color") text_color = strtoul(val.c_str(), NULL, 0);
    else if (key == "ghost_color") ghost_color = strtoul(val.c_str(), NULL, 0);
    else if (key == "bg_color") bg_color = strtoul(val.c_str(), NULL, 0);
    else if (key == "sel_color") sel_color = strtoul(val.c_str(), NULL, 0);
    else if (key == "auto_indent") auto_indent = atoi(val.c_str()) != 0;
    else if (key == "tab_spaces") tab_spaces = atoi(val.c_str());
    else if (key == "word_wrap") word_wrap = atoi(val.c_str()) != 0;
    else if (key == "line_numbers") line_numbers = atoi(val.c_str()) != 0;
    else if (key == "ac_delay_ms") ac_delay_ms = atoi(val.c_str());
    else if (key == "ai_launch_path") ai_launch_path = json_unescape(val);
    else if (key == "ai_host") ai_host = json_unescape(val);
    else if (key == "ai_port") ai_port = atoi(val.c_str());
    else if (key == "ai_infill") ai_infill = atoi(val.c_str()) != 0;
    else if (key == "ai_system_prompt") ai_system_prompt = json_unescape(val);
    else if (key == "ai_prefix_chars") ai_prefix_chars = atoi(val.c_str());
    else if (key == "ai_suffix_chars") ai_suffix_chars = atoi(val.c_str());
    else if (key == "ai_timeout_ms") ai_timeout_ms = atoi(val.c_str());
    else if (key == "ai_context_length") ai_context_length = atoi(val.c_str());
    else if (key == "ai_temperature") ai_temperature = atof(val.c_str());
    else if (key == "ai_top_p") ai_top_p = atof(val.c_str());
    else if (key == "ai_top_k") ai_top_k = atoi(val.c_str());
    else if (key == "ai_cache_prompt") ai_cache_prompt = atoi(val.c_str()) != 0;
    else if (key == "ai_slot_id") ai_slot_id = atoi(val.c_str());
    else if (key == "ai_tq") ai_tq = atoi(val.c_str()) != 0;
    else if (key == "save_backup") save_backup = atoi(val.c_str()) != 0;
    else if (key == "stop_punct") stop_punct = atoi(val.c_str()) != 0;
}

string app_prefs::save_all() const
{
    ostringstream out;

    out << "win_w=" << win_w << "\n";
    out << "win_h=" << win_h << "\n";
    out << "dict_path=" << json_escape(dict_path) << "\n";
    out << "autocomp_mode=" << autocomp_mode << "\n";
    out << "cont_autocomp=" << cont_autocomp << "\n";
    out << "max_suggestion=" << max_suggestion << "\n";
    out << "model_path=" << json_escape(model_path) << "\n";
    out << "text_font_name=" << json_escape(text_font_name) << "\n";
    out << "text_size=" << text_size << "\n";
    out << "text_color=0x" << hex << text_color << dec << "\n";
    out << "ghost_color=0x" << hex << ghost_color << dec << "\n";
    out << "bg_color=0x" << hex << bg_color << dec << "\n";
    out << "sel_color=0x" << hex << sel_color << dec << "\n";
    out << "auto_indent=" << auto_indent << "\n";
    out << "tab_spaces=" << tab_spaces << "\n";
    out << "word_wrap=" << word_wrap << "\n";
    out << "line_numbers=" << line_numbers << "\n";
    out << "ac_delay_ms=" << ac_delay_ms << "\n";
    out << "ai_launch_path=" << json_escape(ai_launch_path) << "\n";
    out << "ai_host=" << json_escape(ai_host) << "\n";
    out << "ai_port=" << ai_port << "\n";
    out << "ai_infill=" << ai_infill << "\n";
    out << "ai_system_prompt=" << json_escape(ai_system_prompt) << "\n";
    out << "ai_prefix_chars=" << ai_prefix_chars << "\n";
    out << "ai_suffix_chars=" << ai_suffix_chars << "\n";
    out << "ai_timeout_ms=" << ai_timeout_ms << "\n";
    out << "ai_context_length=" << ai_context_length << "\n";
    out << "ai_temperature=" << ai_temperature << "\n";
    out << "ai_top_p=" << ai_top_p << "\n";
    out << "ai_top_k=" << ai_top_k << "\n";
    out << "ai_cache_prompt=" << ai_cache_prompt << "\n";
    out << "ai_slot_id=" << ai_slot_id << "\n";
    out << "ai_tq=" << ai_tq << "\n";
    out << "save_backup=" << save_backup << "\n";
    out << "stop_punct=" << stop_punct << "\n";
    return out.str();
}

void app_prefs::store_preset(string name)
{
    sets[name] = save_all();
}

void app_prefs::use_preset(string name)
{
    map<string, string>::iterator it = sets.find(name);
    if (it == sets.end()) return;

    set_defaults();
    istringstream in(it->second);
    string line;
    while (getline(in, line)) {
        size_t p = line.find('=');
        if (p == string::npos) continue;
        load_item(line.substr(0, p), line.substr(p + 1));
    }
    last_preset = name;
}

void app_prefs::del_preset(string name)
{
    if (name == PREFS_MAIN) return;
    sets.erase(name);
    if (last_preset == name) last_preset = PREFS_MAIN;
}

void app_prefs::load()
{
    set_defaults();
    sets.clear();

    string dir = config_dir();
    string path = dir + "/" + CONFIG_FILE;
    ifstream file(path.c_str());

    string first;
    if (file.is_open()) {
        string sect, body, line;
        while (getline(file, line)) {
            if (line.size() >= 3 && line[0] == '[' && line[line.size() - 1] == ']') {
                if (!sect.empty()) sets[sect] = body;
                sect = line.substr(1, line.size() - 2);
                if (first.empty()) first = sect;
                body.clear();
                continue;
            }
            if (sect.empty()) continue;
            body += line + "\n";
        }
        if (!sect.empty() && !body.empty()) sets[sect] = body;
    }

    if (sets.find(PREFS_MAIN) == sets.end()) {
        last_preset = PREFS_MAIN;
        store_preset(PREFS_MAIN);
        return;
    }

    if (!first.empty()) use_preset(first);
    else use_preset(PREFS_MAIN);
}

void app_prefs::save()
{
    string dir = config_dir();
    mkdir(dir.c_str(), CONFIG_MODE);

    string path = dir + "/" + CONFIG_FILE;
    ofstream file(path.c_str());
    if (!file.is_open()) return;

    map<string, string> tmp = sets;
    tmp[last_preset] = save_all();

    file << "[" << last_preset << "]\n" << tmp[last_preset];
    for (map<string, string>::const_iterator it = tmp.begin(); it != tmp.end(); ++it) {
        if (it->first == last_preset) continue;
        file << "[" << it->first << "]\n" << it->second;
    }
}
