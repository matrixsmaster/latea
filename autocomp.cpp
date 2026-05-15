#include <FL/Fl.H>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include "common.h"
#include "autocomp.h"
#include "editor.h"

void autocomp::timer_cb(void *data)
{
    autocomp *cmpt = (autocomp *)data;
    if (now_seconds() < cmpt->due_time) {
        Fl::repeat_timeout(AUTOCOMPLETE_POLL_SEC, timer_cb, data);
        return;
    }
    cmpt->timer_active = false;
    cmpt->trigger_now();
}

autocomp::~autocomp()
{
    cancel_timer();
}

void autocomp::cancel_pending()
{
    cancel_timer();
}

void autocomp::on_preferences_changed()
{
    cancel_timer();
    g_wnd->clear_suggestion();
}

void autocomp::on_text_changed(int, int, int, const char *)
{
    if (g_wnd->suppress_autocomp || g_wnd->history.replaying) return;
    g_wnd->clear_suggestion();
    if (g_wnd->prefs.cont_autocomp) schedule();
}

void autocomp::on_cursor_changed()
{
    if (g_wnd->suppress_autocomp) return;
    g_wnd->clear_suggestion();
    if (g_wnd->prefs.cont_autocomp) schedule();
}

void autocomp::schedule()
{
    double delay = AUTOCOMPLETE_DELAY_SEC;

    if (g_wnd->suppress_autocomp || !g_wnd->prefs.cont_autocomp) return;
    if ((g_wnd->prefs.autocomp_mode == AUTOCOMPLETE_AI || g_wnd->prefs.autocomp_mode == AUTOCOMPLETE_EMBEDDED_AI) && g_wnd->prefs.ai_delay_ms > 0) delay = g_wnd->prefs.ai_delay_ms / 1000.0;
    due_time = now_seconds() + delay;
    if (timer_active) return;
    timer_active = true;
    Fl::add_timeout(AUTOCOMPLETE_POLL_SEC, timer_cb, this);
}

void autocomp::trigger_now()
{
    std::string text;
    int anchor_pos = 0;

    cancel_timer();
    if (!can_complete()) {
        g_wnd->clear_suggestion();
        return;
    }

    if (!complete(text, anchor_pos)) {
        g_wnd->clear_suggestion();
        return;
    }
    publish(text, anchor_pos);
}

bool autocomp::can_complete() const
{
    if (g_wnd->suppress_autocomp) return false;
    if (g_wnd->prefs.autocomp_mode == AUTOCOMPLETE_DISABLED) return false;
    if (g_wnd->prefs.max_suggestion_chars <= 0) return false;
    return true;
}

bool autocomp::current_word(std::string &prefix, int &anchor_pos) const
{
    int pos = g_wnd->editor->insert_position();
    int start = pos;
    if (g_wnd->textbuf->selected()) return false;
    while (start > 0) {
        char c = g_wnd->textbuf->char_at(start - 1);
        if (!is_word_char(c)) break;
        start--;
    }
    if (start == pos) return false;

    char *range = g_wnd->textbuf->text_range(start, pos);
    prefix = range ? range : "";
    free(range);
    if (prefix.empty()) return false;
    anchor_pos = pos;
    return true;
}

void autocomp::publish(const std::string &text, int anchor_pos, int request_id)
{
    g_wnd->set_suggestion(text, anchor_pos, request_id);
}

void autocomp::cancel_timer()
{
    if (!timer_active) return;
    Fl::remove_timeout(timer_cb, this);
    timer_active = false;
}

void autocomp_dict::on_preferences_changed()
{
    autocomp::on_preferences_changed();
    if (loaded_path == g_wnd->prefs.dict_path) return;
    loaded_path.clear();
    words.clear();
}

bool autocomp_dict::complete(std::string &text, int &anchor_pos)
{
    std::string prefix;
    std::string prefix_lower;
    if (!current_word(prefix, anchor_pos)) return 0;
    if (!g_wnd->prefs.dict_path.size()) return false;
    if (loaded_path != g_wnd->prefs.dict_path) {
        std::ifstream file(g_wnd->prefs.dict_path.c_str());
        std::string word;
        words.clear();
        while (file >> word) words.push_back(word);
        loaded_path = g_wnd->prefs.dict_path;
    }

    prefix_lower = lowercase(prefix);
    for (size_t i = 0; i < words.size(); i++) {
        if (words[i].size() <= prefix.size()) continue;
        if (lowercase(words[i].substr(0, prefix.size())) != prefix_lower) continue;
        text = words[i].substr(prefix.size());
        return !text.empty();
    }
    return false;
}

bool autocomp_file::complete(std::string &text, int &anchor_pos)
{
    std::string prefix;
    std::set<std::string> options;
    if (!current_word(prefix, anchor_pos)) return 0;

    char *all = g_wnd->textbuf->text();
    std::string best;
    for (char *p = all; p && *p;) {
        while (*p && !is_word_char(*p)) p++;
        if (!*p) break;
        char *start = p;
        while (*p && is_word_char(*p)) p++;
        std::string word(start, p - start);
        if (word.size() <= prefix.size()) continue;
        if (word.compare(0, prefix.size(), prefix) != 0) continue;
        options.insert(word);
    }
    if (all) free(all);

    for (std::set<std::string>::iterator it = options.begin(); it != options.end(); ++it) {
        if (best.empty() || it->size() < best.size()) best = *it;
    }
    if (best.empty()) return false;
    text = best.substr(prefix.size());
    return !text.empty();
}

autocomp_ai::autocomp_ai()
{
    pthread_mutex_init(mutex(), NULL);
    worker_running = false;
    worker_done = false;
    worker_ok = false;
    worker_joined = true;
    poll_active = false;
    have_queued = false;
    next_serial = 0;
    latest_serial = 0;
}

autocomp_ai::~autocomp_ai()
{
    cancel_timer();
    if (poll_active) Fl::remove_timeout(poll_cb, this);
    if (worker_running && !worker_joined) pthread_join(worker, NULL);
    pthread_mutex_destroy(mutex());
}

void autocomp_ai::on_preferences_changed()
{
    autocomp::on_preferences_changed();
    cancel_pending();
    on_preferences_changed_backend();
}

void autocomp_ai::trigger_now()
{
    cancel_timer();
    if (!can_complete()) {
        g_wnd->clear_suggestion();
        return;
    }
    if (!build_request()) {
        g_wnd->clear_suggestion();
        return;
    }
    queue_or_start();
}

void autocomp_ai::cancel_pending()
{
    pthread_mutex_lock(mutex());
    have_queued = false;
    pthread_mutex_unlock(mutex());
    cancel_timer();
    on_cancel_pending_backend();
}

bool autocomp_ai::is_busy() const
{
    return worker_running || have_queued;
}

bool autocomp_ai::complete(std::string &, int &)
{
    return false;
}

bool autocomp_ai::build_request_text(std::string &prefix, std::string &suffix, int &anchor_pos)
{
    int pos;
    int before;
    int after;
    char *prefix_text;
    char *suffix_text;

    if (g_wnd->textbuf->selected()) return false;

    pos = g_wnd->editor->insert_position();
    before = g_wnd->prefs.ai_prefix_chars? MAX(0, pos - g_wnd->prefs.ai_prefix_chars) : 0;
    after = MIN(g_wnd->textbuf->length(), pos + g_wnd->prefs.ai_suffix_chars);
    prefix_text = g_wnd->textbuf->text_range(before, pos);
    suffix_text = (pos == after)? NULL : g_wnd->textbuf->text_range(pos, after);
    prefix = prefix_text ? prefix_text : "";
    suffix = suffix_text ? suffix_text : "";
    anchor_pos = pos;

    if (prefix_text) free(prefix_text);
    if (suffix_text) free(suffix_text);
    return true;
}

int autocomp_ai::next_request_serial()
{
    latest_serial = ++next_serial;
    return latest_serial;
}

void autocomp_ai::start_worker()
{
    pthread_mutex_lock(mutex());
    activate_request_locked();
    worker_done = false;
    worker_ok = false;
    worker_running = true;
    worker_joined = false;
    on_worker_started_locked();
    pthread_mutex_unlock(mutex());

    if (pthread_create(&worker, NULL, worker_main, this) != 0) {
        pthread_mutex_lock(mutex());
        worker_running = false;
        worker_joined = true;
        pthread_mutex_unlock(mutex());
        g_wnd->update_ai_status((std::string(ai_label()) + " worker start failed").c_str());
        return;
    }

    g_wnd->update_ai_status((std::string(ai_label()) + " request pending").c_str());
    if (!poll_active) {
        poll_active = true;
        Fl::add_timeout(AUTOCOMPLETE_POLL_SEC, poll_cb, this);
    }
}

void autocomp_ai::queue_or_start()
{
    if (!worker_running) {
        start_worker();
        return;
    }
    pthread_mutex_lock(mutex());
    queue_request_locked();
    have_queued = true;
    pthread_mutex_unlock(mutex());
    on_request_queued();
    g_wnd->update_ai_status((std::string(ai_label()) + " request queued").c_str());
}

void autocomp_ai::finish_worker()
{
    bool backend_action = false;
    bool start_queued = false;
    bool stale = false;
    ai_result res;
    bool ok = false;

    pthread_mutex_lock(mutex());
    ok = worker_ok;
    res = finished_res;
    if (!worker_joined) {
        pthread_mutex_unlock(mutex());
        pthread_join(worker, NULL);
        pthread_mutex_lock(mutex());
        worker_joined = true;
    }
    worker_running = false;
    worker_done = false;
    stale = res.serial != latest_serial;
    if (have_queued) {
        load_queued_request_locked();
        have_queued = false;
        start_queued = true;
        stale = true;
    }
    on_finish_locked(backend_action);
    pthread_mutex_unlock(mutex());

    after_finish_unlocked(backend_action);

    if (!stale && ok && g_wnd->editor->insert_position() == res.anchor_pos && !g_wnd->suppress_autocomp) {
        publish(res.text, res.anchor_pos, res.serial);
        g_wnd->update_ai_status((std::string(ai_label()) + " suggestion ready").c_str());
    } else if (!stale && !ok && !res.error.empty()) {
        g_wnd->update_ai_status(res.error.c_str());
        g_wnd->clear_suggestion();
    } else if (stale) {
        g_wnd->update_ai_status((std::string(ai_label()) + " stale result discarded").c_str());
    }

    if (start_queued) start_worker();
}

void *autocomp_ai::worker_main(void *data)
{
    autocomp_ai *self = (autocomp_ai *)data;
    ai_result res;
    bool ok = self->run_active_request(res);

    pthread_mutex_lock(self->mutex());
    self->finished_res = res;
    self->worker_ok = ok;
    self->worker_done = true;
    pthread_mutex_unlock(self->mutex());
    Fl::awake();
    return NULL;
}

void autocomp_ai::poll_cb(void *data)
{
    autocomp_ai *self = (autocomp_ai *)data;
    bool done;
    bool running;

    pthread_mutex_lock(self->mutex());
    done = self->worker_done;
    running = self->worker_running;
    pthread_mutex_unlock(self->mutex());

    if (running && !done) self->poll_running();
    if (done) self->finish_worker();
    if (self->worker_running || self->have_queued) {
        Fl::repeat_timeout(AUTOCOMPLETE_POLL_SEC, poll_cb, data);
        return;
    }
    self->poll_active = false;
}

bool autocomp_lan_ai::build_request()
{
    if (!build_request_text(pending_req.prefix, pending_req.suffix, pending_req.anchor_pos)) return false;
    pending_req.serial = next_request_serial();
    pending_req.system_prompt = g_wnd->prefs.ai_system_prompt;
    pending_req.endpoint_mode = g_wnd->prefs.ai_endpoint_mode;
    pending_req.max_chars = g_wnd->prefs.max_suggestion_chars;
    pending_req.timeout_ms = g_wnd->prefs.ai_timeout_ms;
    pending_req.context_length = g_wnd->prefs.ai_context_length;
    pending_req.temperature = g_wnd->prefs.ai_temperature;
    pending_req.top_p = g_wnd->prefs.ai_top_p;
    pending_req.top_k = g_wnd->prefs.ai_top_k;
    pending_req.cache_prompt = g_wnd->prefs.ai_cache_prompt;
    pending_req.slot_id = g_wnd->prefs.ai_slot_id;
    pending_req.host = g_wnd->prefs.ai_host;
    pending_req.port = g_wnd->prefs.ai_port;
    return true;
}

bool autocomp_lan_ai::run_active_request(ai_result &res)
{
    llama_client client;
    ai_request req;

    pthread_mutex_lock(mutex());
    req = active_req;
    pthread_mutex_unlock(mutex());

    res.serial = req.serial;
    res.anchor_pos = req.anchor_pos;
    if (req.endpoint_mode == AI_ENDPOINT_PREFER_INFILL && !req.suffix.empty()) return client.request_infill(req, res);
    return client.request_completion(req, res);
}

const char* autocomp_lan_ai::ai_label() const
{
    return "AI";
}

void autocomp_lan_ai::activate_request_locked()
{
    active_req = pending_req;
}

void autocomp_lan_ai::queue_request_locked()
{
    queued_req = pending_req;
}

void autocomp_lan_ai::load_queued_request_locked()
{
    active_req = queued_req;
}

autocomp_emb_ai::~autocomp_emb_ai()
{
    engine.request_stop();
}

bool autocomp_emb_ai::build_request()
{
    if (!build_request_text(pending_req.prefix, pending_req.suffix, pending_req.anchor_pos)) return false;
    pending_req.serial = next_request_serial();
    pending_req.model_path = g_wnd->prefs.model_path;
    pending_req.max_chars = g_wnd->prefs.max_suggestion_chars;
    pending_req.context_length = g_wnd->prefs.ai_context_length;
    pending_req.top_k = g_wnd->prefs.ai_top_k;
    pending_req.top_p = g_wnd->prefs.ai_top_p;
    pending_req.temperature = g_wnd->prefs.ai_temperature;
    pending_req.cache_prompt = g_wnd->prefs.ai_cache_prompt;
    return true;
}

bool autocomp_emb_ai::run_active_request(ai_result &res)
{
    emb_ai_request req;
    emb_ai_result er;

    pthread_mutex_lock(mutex());
    req = active_req;
    pthread_mutex_unlock(mutex());

    res.serial = req.serial;
    res.anchor_pos = req.anchor_pos;
    if (!engine.complete(req, er)) {
        res.text = er.text;
        res.error = er.error;
        return false;
    }
    res.text = er.text;
    res.error = er.error;
    return true;
}

const char* autocomp_emb_ai::ai_label() const
{
    return "Embedded AI";
}

void autocomp_emb_ai::activate_request_locked()
{
    active_req = pending_req;
}

void autocomp_emb_ai::queue_request_locked()
{
    queued_req = pending_req;
}

void autocomp_emb_ai::load_queued_request_locked()
{
    active_req = queued_req;
}

void autocomp_emb_ai::on_cancel_pending_backend()
{
    engine.request_stop();
}

void autocomp_emb_ai::on_preferences_changed_backend()
{
    pthread_mutex_lock(mutex());
    if (busy_running()) unload_requested = true;
    else engine.unload();
    pthread_mutex_unlock(mutex());
}

void autocomp_emb_ai::on_worker_started_locked()
{
    unload_requested = false;
}

void autocomp_emb_ai::on_request_queued()
{
    engine.request_stop();
}

void autocomp_emb_ai::on_finish_locked(bool &backend_action)
{
    backend_action = unload_requested;
    unload_requested = false;
}

void autocomp_emb_ai::after_finish_unlocked(bool backend_action)
{
    if (backend_action) engine.unload();
}

void autocomp_emb_ai::poll_running()
{
    std::string partial;
    emb_ai_request req;
    int serial;

    pthread_mutex_lock(mutex());
    req = active_req;
    serial = current_serial();
    pthread_mutex_unlock(mutex());

    engine.get_partial_text(partial);
    if (!partial.empty() && req.serial == serial && g_wnd->editor->insert_position() == req.anchor_pos && !g_wnd->suppress_autocomp) {
        publish(partial, req.anchor_pos, req.serial);
        g_wnd->update_ai_status("Embedded AI generating");
    }
}
