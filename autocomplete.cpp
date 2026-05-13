#include <FL/Fl.H>
#include <fstream>
#include "common.h"
#include "editor.h"
#include "autocomplete.h"

autocomplete_engine::autocomplete_engine()
{
    owner = 0;
    latest_serial = 0;
    worker_running = false;
    has_pending = false;
    has_result = false;
}

autocomplete_engine::~autocomplete_engine()
{
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        worker_running = false;
        has_pending = false;
    }
    worker_cv.notify_all();
    if (worker_thread.joinable()) worker_thread.join();
}

void autocomplete_engine::set_owner(editor_window *window)
{
    owner = window;
}

void autocomplete_engine::timeout_cb(void *data)
{
    ((autocomplete_engine *)data)->trigger_now();
}

void autocomplete_engine::awake_cb(void *data)
{
    ((autocomplete_engine *)data)->handle_awake();
}

void autocomplete_engine::schedule()
{
    if (!owner) return;
    Fl::remove_timeout(timeout_cb, this);
    if (!owner->prefs.continuous_autocomplete) return;
    Fl::add_timeout(owner->prefs.ai_delay_ms / 1000.0, timeout_cb, this);
}

void autocomplete_engine::trigger_now()
{
    if (!owner) return;
    Fl::remove_timeout(timeout_cb, this);
    latest_serial++;

    if (owner->prefs.autocomplete_mode == AUTOCOMPLETE_DISABLED) {
        owner->clear_suggestion();
        return;
    }
    if (owner->prefs.autocomplete_mode == AUTOCOMPLETE_DICTIONARY_FILE) {
        publish_local_result(complete_dictionary_file());
        return;
    }
    if (owner->prefs.autocomplete_mode == AUTOCOMPLETE_CURRENT_FILE) {
        publish_local_result(complete_current_file());
        return;
    }
    request_ai_now();
}

void autocomplete_engine::cancel_visible()
{
    if (!owner) return;
    owner->clear_suggestion();
}

void autocomplete_engine::on_text_changed()
{
    cancel_visible();
    schedule();
}

void autocomplete_engine::on_cursor_changed()
{
    if (!owner) return;
    if (owner->editor->insert_position() != owner->suggestion.anchor_pos) {
        owner->clear_suggestion();
    }
}

void autocomplete_engine::on_preferences_changed()
{
    Fl::remove_timeout(timeout_cb, this);
    cancel_visible();
}

void autocomplete_engine::ensure_worker()
{
    if (worker_thread.joinable()) return;
    worker_running = true;
    worker_thread = std::thread(&autocomplete_engine::worker_main, this);
}

void autocomplete_engine::worker_main()
{
    for (;;) {
        ai_request req;
        {
            std::unique_lock<std::mutex> lock(worker_mutex);
            worker_cv.wait(lock, [this]() { return has_pending || !worker_running; });
            if (!worker_running) break;
            req = pending_request;
            has_pending = false;
        }

        ai_result result;
        result.serial = req.serial;
        result.anchor_pos = req.anchor_pos;

        bool ok = false;
        if (req.endpoint_mode == AI_ENDPOINT_PREFER_INFILL && !req.suffix.empty()) {
            ok = client.request_infill(req, result);
        }
        if (!ok) ok = client.request_completion(req, result);
        result.text = sanitize_suggestion(result.text, req.max_chars);

        {
            std::lock_guard<std::mutex> lock(worker_mutex);
            ready_result = result;
            has_result = true;
        }
        Fl::awake(awake_cb, this);
    }
}

void autocomplete_engine::request_ai_now()
{
    if (!owner) return;
    int anchor = owner->editor->insert_position();
    char *all = owner->textbuf->text();
    std::string text = all ? all : "";
    free(all);

    int prefix_start = anchor - owner->prefs.ai_prefix_chars;
    if (prefix_start < 0) prefix_start = 0;
    int suffix_end = anchor + owner->prefs.ai_suffix_chars;
    if (suffix_end > (int)text.size()) suffix_end = (int)text.size();

    ai_request req;
    req.serial = latest_serial;
    req.anchor_pos = anchor;
    req.prefix = text.substr(prefix_start, anchor - prefix_start);
    req.suffix = text.substr(anchor, suffix_end - anchor);
    req.system_prompt = owner->prefs.ai_system_prompt;
    req.endpoint_mode = owner->prefs.ai_endpoint_mode;
    req.max_chars = owner->prefs.max_suggestion_chars;
    req.timeout_ms = owner->prefs.ai_timeout_ms;
    req.temperature = owner->prefs.ai_temperature;
    req.top_p = owner->prefs.ai_top_p;
    req.cache_prompt = owner->prefs.ai_cache_prompt;
    req.slot_id = owner->prefs.ai_slot_id;
    req.host = owner->prefs.ai_host;
    req.port = owner->prefs.ai_port;

    ensure_worker();
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        pending_request = req;
        has_pending = true;
    }
    worker_cv.notify_one();
    owner->update_ai_status("waiting for AI");
}

void autocomplete_engine::publish_local_result(const std::string &suffix)
{
    if (!owner) return;
    if (suffix.empty()) {
        owner->clear_suggestion();
        return;
    }
    owner->set_suggestion(sanitize_suggestion(suffix, owner->prefs.max_suggestion_chars),
        owner->editor->insert_position(), latest_serial);
}

std::string autocomplete_engine::current_word_prefix() const
{
    if (!owner) return "";
    int pos = owner->editor->insert_position();
    if (pos <= 0) return "";
    int start = pos;
    while (start > 0 && is_word_char(owner->textbuf->byte_at(start - 1))) start--;
    if (start == pos) return "";
    char *prefix = owner->textbuf->text_range(start, pos);
    std::string out = prefix ? prefix : "";
    free(prefix);
    return out;
}

void autocomplete_engine::load_dictionary_if_needed()
{
    if (!owner) return;
    if (loaded_dictionary_path == owner->prefs.dictionary_path) return;
    loaded_dictionary_path = owner->prefs.dictionary_path;
    dictionary_words.clear();
    if (loaded_dictionary_path.empty()) return;

    std::ifstream file(loaded_dictionary_path.c_str());
    std::string line;
    while (std::getline(file, line)) {
        line = trim_newlines(line);
        if (!line.empty()) dictionary_words.push_back(line);
    }
}

std::string autocomplete_engine::find_completion_from_words(const std::vector<std::string> &words,
    const std::string &prefix) const
{
    if (prefix.empty()) return "";

    std::string prefix_lower = lowercase(prefix);
    std::string best;
    for (size_t i = 0; i < words.size(); i++) {
        const std::string &word = words[i];
        if ((int)word.size() <= (int)prefix.size()) continue;
        if (word.compare(0, prefix.size(), prefix) == 0 ||
                lowercase(word).compare(0, prefix_lower.size(), prefix_lower) == 0) {
            if (best.empty() || word.size() < best.size()) best = word;
        }
    }

    if (best.empty()) return "";
    return best.substr(prefix.size());
}

std::string autocomplete_engine::complete_dictionary_file()
{
    load_dictionary_if_needed();
    return find_completion_from_words(dictionary_words, current_word_prefix());
}

std::string autocomplete_engine::complete_current_file()
{
    if (!owner) return "";
    char *all = owner->textbuf->text();
    std::string text = all ? all : "";
    free(all);

    std::set<std::string> unique_words;
    std::string word;
    for (size_t i = 0; i < text.size(); i++) {
        if (is_word_char(text[i])) word += text[i];
        else if (!word.empty()) {
            unique_words.insert(word);
            word.clear();
        }
    }
    if (!word.empty()) unique_words.insert(word);

    std::vector<std::string> words(unique_words.begin(), unique_words.end());
    return find_completion_from_words(words, current_word_prefix());
}

void autocomplete_engine::handle_awake()
{
    ai_result result;
    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        if (!has_result) return;
        result = ready_result;
        has_result = false;
    }

    if (!owner) return;
    if (result.serial != latest_serial) return;
    if (!result.error.empty()) {
        owner->clear_suggestion();
        owner->update_ai_status(result.error.c_str());
        return;
    }
    if (owner->editor->insert_position() != result.anchor_pos) return;
    if (result.text.empty()) {
        owner->clear_suggestion();
        owner->update_ai_status("no suggestion");
        return;
    }
    owner->set_suggestion(result.text, result.anchor_pos, result.serial);
    owner->update_ai_status("AI suggestion ready");
}
