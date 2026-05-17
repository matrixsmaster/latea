#include <FL/Fl.H>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <set>
#include "common.h"
#include "autocomp.h"
#include "editor.h"

autocomp::~autocomp()
{
    cancel_timer();
}

void autocomp::cancel_pending()
{
    cancel_timer();
    reset_state();
}

void autocomp::on_preferences_changed()
{
    cancel_timer();
    reset_state();
    g_wnd->clear_suggestion();
}

void autocomp::on_text_changed(int, int, int, const char *)
{
    if (g_wnd->suppress_autocomp || g_wnd->history.replaying) return;
    reset_state();
    g_wnd->clear_suggestion();
    if (g_wnd->prefs.cont_autocomp) schedule();
}

void autocomp::on_cursor_changed()
{
    if (g_wnd->suppress_autocomp) return;
    reset_state();
    g_wnd->clear_suggestion();
}

static void timer_cb(void* data)
{
    autocomp* cmpt = (autocomp*)data;
    if (now_seconds() < cmpt->due_time) {
        Fl::repeat_timeout(AUTOCOMPLETE_POLL_SEC, timer_cb, data);
        return;
    }
    cmpt->timer_active = false;
    cmpt->trigger_now();
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
    int pos;
    if (!current_word(prefix, anchor_pos)) return 0;
    if (!g_wnd->prefs.dict_path.size()) return false;
    if (loaded_path != g_wnd->prefs.dict_path) {
        std::ifstream file(g_wnd->prefs.dict_path.c_str());
        std::string word;
        words.clear();
        while (file >> word) words.push_back(word);
        loaded_path = g_wnd->prefs.dict_path;
    }

    opts.clear();
    opt_idx = 0;
    opt_anchor = anchor_pos;
    prefix_lower = lowercase(prefix);
    pos = g_wnd->editor->insert_position();
    for (size_t i = 0; i < words.size(); i++) {
        if (words[i].size() <= prefix.size()) continue;
        if (lowercase(words[i].substr(0, prefix.size())) != prefix_lower) continue;
        if (opt_anchor != pos) continue;
        opts.push_back(words[i].substr(prefix.size()));
    }
    if (opts.empty()) return false;
    g_wnd->set_suggestion_list(opts, 0, anchor_pos);
    text = opts[0];
    return true;
}

bool autocomp_file::complete(std::string &text, int &anchor_pos)
{
    std::string prefix;
    std::set<std::string> options;
    int pos;
    if (!current_word(prefix, anchor_pos)) return 0;

    opts.clear();
    opt_idx = 0;
    opt_anchor = anchor_pos;
    pos = g_wnd->editor->insert_position();
    char *all = g_wnd->textbuf->text();
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
        if (opt_anchor != pos) continue;
        opts.push_back(it->substr(prefix.size()));
    }
    if (opts.empty()) return false;
    g_wnd->set_suggestion_list(opts, 0, anchor_pos);
    text = opts[0];
    return true;
}

bool autocomp_dict::move_suggestion(int dir)
{
    if (!g_wnd->suggest.visible || g_wnd->editor->insert_position() != opt_anchor || opts.empty()) return false;
    if (dir < 0) {
        if (opt_idx <= 0) return false;
        opt_idx--;
    } else {
        if (opt_idx + 1 >= (int)opts.size()) return false;
        opt_idx++;
    }
    g_wnd->set_suggestion_list(opts, opt_idx, opt_anchor);
    return true;
}

bool autocomp_file::move_suggestion(int dir)
{
    if (!g_wnd->suggest.visible || g_wnd->editor->insert_position() != opt_anchor || opts.empty()) return false;
    if (dir < 0) {
        if (opt_idx <= 0) return false;
        opt_idx--;
    } else {
        if (opt_idx + 1 >= (int)opts.size()) return false;
        opt_idx++;
    }
    g_wnd->set_suggestion_list(opts, opt_idx, opt_anchor);
    return true;
}

static void poll_cb(void *data)
{
    autocomp_ai *self = (autocomp_ai *)data;
    bool done;
    bool running;
    bool queued;

    pthread_mutex_lock(&self->lock);
    done = self->worker_done;
    running = self->worker_running;
    queued = self->have_queued;
    pthread_mutex_unlock(&self->lock);

    if (running && !done) self->poll_running();
    if (done) self->finish_worker();

    pthread_mutex_lock(&self->lock);
    running = self->worker_running;
    queued = self->have_queued;
    pthread_mutex_unlock(&self->lock);
    if (running || queued) {
        Fl::repeat_timeout(AUTOCOMPLETE_POLL_SEC, poll_cb, data);
        return;
    }
    self->poll_active = false;
}

autocomp_ai::autocomp_ai()
{
    pthread_mutex_init(&lock, NULL);
    worker_running = false;
    worker_done = false;
    worker_ok = false;
    worker_joined = true;
    poll_active = false;
    have_queued = false;
    next_serial = 0;
    last_serial = 0;
}

autocomp_ai::~autocomp_ai()
{
    cancel_timer();
    if (poll_active) Fl::remove_timeout(poll_cb, this);
    if (worker_running && !worker_joined) pthread_join(worker, NULL);
    pthread_mutex_destroy(&lock);
}

void autocomp_ai::on_preferences_changed()
{
    autocomp::on_preferences_changed();
    cancel_pending();
    on_preferences_changed_backend();
}

void autocomp_ai::on_text_changed(int pos, int inserted, int deleted, const char* deleted_text)
{
    cancel_pending();
    autocomp::on_text_changed(pos, inserted, deleted, deleted_text);
}

void autocomp_ai::on_cursor_changed()
{
    cancel_pending();
    autocomp::on_cursor_changed();
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
    invalidate_serial();
    pthread_mutex_lock(&lock);
    have_queued = false;
    pthread_mutex_unlock(&lock);
    cancel_timer();
    on_cancel_pending_backend();
}

bool autocomp_ai::is_busy() const
{
    return worker_running || have_queued;
}

bool autocomp_ai::move_suggestion(int dir)
{
    if (!g_wnd->suggest.visible || g_wnd->editor->insert_position() != g_wnd->suggest.anchor_pos) return false;
    if (dir < 0) {
        if (g_wnd->suggest.var_idx <= 0) return false;
        g_wnd->set_suggestion_list(g_wnd->suggest.vars, g_wnd->suggest.var_idx - 1, g_wnd->suggest.anchor_pos, g_wnd->suggest.request_id);
        return true;
    }
    if (g_wnd->suggest.var_idx + 1 < (int)g_wnd->suggest.vars.size()) {
        g_wnd->set_suggestion_list(g_wnd->suggest.vars, g_wnd->suggest.var_idx + 1, g_wnd->suggest.anchor_pos, g_wnd->suggest.request_id);
        return true;
    }
    trigger_now();
    return true;
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
    last_serial = ++next_serial;
    return last_serial;
}

void autocomp_ai::reset_state()
{
    invalidate_serial();
}

void autocomp_ai::invalidate_serial()
{
    last_serial = ++next_serial;
}

static void* worker_main(void* data)
{
    autocomp_ai* self = (autocomp_ai*)data;
    ai_result res;
    bool ok = self->run_active_request(res);

    pthread_mutex_lock(&self->lock);
    self->finished_res = res;
    self->worker_ok = ok;
    self->worker_done = true;
    pthread_mutex_unlock(&self->lock);
    Fl::awake();
    return NULL;
}


void autocomp_ai::start_worker()
{
    pthread_mutex_lock(&lock);
    activate_request_locked();
    worker_done = false;
    worker_ok = false;
    worker_running = true;
    worker_joined = false;
    on_worker_started_locked();
    pthread_mutex_unlock(&lock);

    if (pthread_create(&worker, NULL, worker_main, this) != 0) {
        pthread_mutex_lock(&lock);
        worker_running = false;
        worker_joined = true;
        pthread_mutex_unlock(&lock);
        g_wnd->update_ai_status("AI worker start failed");
        return;
    }

    g_wnd->update_ai_status("AI request pending");
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
    pthread_mutex_lock(&lock);
    queue_request_locked();
    have_queued = true;
    pthread_mutex_unlock(&lock);
    on_request_queued();
    g_wnd->update_ai_status("AI request queued");
}

void autocomp_ai::finish_worker()
{
    bool backend_action = false;
    bool start_queued = false;
    bool stale = false;
    ai_result res;
    bool ok = false;
    int join_rc = 0;

    pthread_mutex_lock(&lock);
    ok = worker_ok;
    res = finished_res;
    if (!worker_joined) {
        pthread_mutex_unlock(&lock);
        join_rc = pthread_tryjoin_np(worker, NULL);
        if (join_rc == EBUSY) return;
        pthread_mutex_lock(&lock);
        worker_joined = join_rc == 0 || join_rc == ESRCH;
    }
    worker_running = false;
    worker_done = false;
    stale = res.serial != last_serial;
    if (have_queued) {
        load_queued_request_locked();
        have_queued = false;
        start_queued = true;
        stale = true;
    }
    on_finish_locked(backend_action);
    pthread_mutex_unlock(&lock);

    after_finish_unlocked(backend_action);

    if (!stale && ok && g_wnd->editor->insert_position() == res.anchor_pos && !g_wnd->suppress_autocomp) {
        publish(res.text, res.anchor_pos, res.serial);
        g_wnd->update_ai_status("AI suggestion ready");
    } else if (!stale && !ok && !res.error.empty()) {
        g_wnd->update_ai_status(res.error.c_str());
        g_wnd->clear_suggestion();
    } else if (stale) {
        g_wnd->update_ai_status("AI stale result discarded");
    }

    if (start_queued) start_worker();
}

bool autocomp_lan_ai::build_request()
{
    if (!build_request_text(pending.prefix, pending.suffix, pending.anchor_pos)) return false;
    pending.serial = next_request_serial();
    pending.system_prompt = g_wnd->prefs.ai_system_prompt;
    pending.model_path = g_wnd->prefs.model_path;
    pending.launch_path = g_wnd->prefs.ai_launch_path;
    pending.endpoint_mode = g_wnd->prefs.ai_endpoint_mode;
    pending.max_chars = g_wnd->prefs.max_suggestion_chars;
    pending.timeout_ms = g_wnd->prefs.ai_timeout_ms;
    pending.context_length = g_wnd->prefs.ai_context_length;
    pending.temperature = g_wnd->prefs.ai_temperature;
    pending.top_p = g_wnd->prefs.ai_top_p;
    pending.top_k = g_wnd->prefs.ai_top_k;
    pending.cache_prompt = g_wnd->prefs.ai_cache_prompt;
    pending.slot_id = g_wnd->prefs.ai_slot_id;
    pending.host = g_wnd->prefs.ai_host;
    pending.port = g_wnd->prefs.ai_port;
    return true;
}

bool autocomp_lan_ai::run_active_request(ai_result &res)
{
    llama_client client;
    ai_request req;

    pthread_mutex_lock(&lock);
    req = active;
    pthread_mutex_unlock(&lock);

    res.serial = req.serial;
    res.anchor_pos = req.anchor_pos;
    if (req.endpoint_mode == AI_ENDPOINT_PREFER_INFILL && !req.suffix.empty()) return client.request_infill(req, res);
    return client.request_completion(req, res);
}

void autocomp_lan_ai::activate_request_locked()
{
    active = pending;
}

void autocomp_lan_ai::queue_request_locked()
{
    queued = pending;
}

void autocomp_lan_ai::load_queued_request_locked()
{
    active = queued;
}

autocomp_emb_ai::~autocomp_emb_ai()
{
    engine.request_stop();
}

bool autocomp_emb_ai::build_request()
{
    if (!build_request_text(pending.prefix, pending.suffix, pending.anchor_pos)) return false;
    pending.serial = next_request_serial();
    pending.model_path = g_wnd->prefs.model_path;
    pending.max_chars = g_wnd->prefs.max_suggestion_chars;
    pending.context_length = g_wnd->prefs.ai_context_length;
    pending.top_k = g_wnd->prefs.ai_top_k;
    pending.top_p = g_wnd->prefs.ai_top_p;
    pending.temperature = g_wnd->prefs.ai_temperature;
    pending.cache_prompt = g_wnd->prefs.ai_cache_prompt;
    pending.tquant = g_wnd->prefs.ai_tq;
    return true;
}

bool autocomp_emb_ai::run_active_request(ai_result &res)
{
    ai_request req;
    ai_result er;

    pthread_mutex_lock(&lock);
    req = active;
    pthread_mutex_unlock(&lock);

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

void autocomp_emb_ai::activate_request_locked()
{
    active = pending;
}

void autocomp_emb_ai::queue_request_locked()
{
    queued = pending;
}

void autocomp_emb_ai::load_queued_request_locked()
{
    active = queued;
}

void autocomp_emb_ai::on_cancel_pending_backend()
{
    engine.request_stop();
    g_wnd->update_ai_usage(0, 0);
}

void autocomp_emb_ai::on_preferences_changed_backend()
{
    pthread_mutex_lock(&lock);
    if (worker_running) unload_requested = true;
    else engine.unload();
    pthread_mutex_unlock(&lock);
    g_wnd->update_ai_usage(0, 0);
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
    if (!backend_action) return;
    engine.unload();
    g_wnd->update_ai_usage(0, 0);
}

void autocomp_emb_ai::poll_running()
{
    pthread_mutex_lock(&lock);
    ai_request req = active;
    int serial = last_serial;
    pthread_mutex_unlock(&lock);

    std::string partial;
    engine.get_partial_text(partial);

    int used = 0, ctx = 0;
    engine.get_usage(used, ctx);
    g_wnd->update_ai_usage(used, ctx);
    if (!partial.empty() && req.serial == serial && g_wnd->editor->insert_position() == req.anchor_pos && !g_wnd->suppress_autocomp) {
        publish(partial, req.anchor_pos, req.serial);
        g_wnd->update_ai_status("AI generating");
    }
}
