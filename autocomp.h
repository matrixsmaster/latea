#pragma once

#include <string>
#include <vector>
#include <map>
#include <pthread.h>
#include "emb_ai.h"
#include "llama_client.h"

#define AUTOCOMPLETE_DELAY_SEC 0.18
#define AUTOCOMPLETE_POLL_SEC 0.05
#define MKV_MAX_OPTS 24
#define MKV_MAX_BEAM 48
#define MKV_MAX_BRANCH 6

enum { W_IDLE, W_RUNNING, W_DONE, W_GONE };

struct autocomp
{
    double due_time = 0;
    bool timer_active = false;
    std::vector<std::string> opts;
    int opt_idx = 0;
    int opt_anchor = 0;

    virtual ~autocomp();

    virtual void on_preferences_changed();
    virtual void on_text_changed(int pos, int inserted, int deleted, const char* deleted_text);
    virtual void on_cursor_changed();
    virtual void schedule();
    virtual void trigger_now();
    virtual void cancel_pending();
    virtual bool is_busy() const { return false; }
    virtual bool move_suggestion(int) { return false; }
    virtual void reset_state() {}
    virtual bool complete(std::string &text, int &anchor_pos) = 0;

    bool can_complete() const;
    bool current_word(std::string &prefix, int &anchor_pos) const;
    void publish(const std::string &text, int anchor_pos, int request_id = 0);
    void cancel_timer();
};

struct autocomp_dict : autocomp
{
    std::string loaded_path;
    std::vector<std::string> words;

    void on_preferences_changed();
    bool move_suggestion(int dir);
    bool complete(std::string &text, int &anchor_pos);
};

struct autocomp_file : autocomp
{
    bool complete(std::string &text, int &anchor_pos);
    bool move_suggestion(int dir);
};

struct autocomp_ai : autocomp
{
    pthread_mutex_t lock;
    pthread_t worker;
    ai_result finished_res;
    int wstate;
    bool worker_ok;
    bool poll_active, have_queued;
    int last_serial, next_serial;
    ai_request pending, active, queued;

    autocomp_ai();
    virtual ~autocomp_ai();

    void invalidate_serial();
    void on_preferences_changed();
    void on_text_changed(int pos, int inserted, int deleted, const char* deleted_text);
    void on_cursor_changed();
    void trigger_now();
    void cancel_pending();
    bool is_busy() const;
    bool move_suggestion(int dir);
    bool complete(std::string&, int&) { return false; }
    bool build_request_text(std::string &prefix, std::string &suffix, int &anchor_pos);
    int next_request_serial();
    void reset_state();
    void start_worker();
    void queue_or_start();
    void finish_worker();

    virtual bool build_request() = 0;
    virtual bool run_active_request(ai_result &res) = 0;
    virtual void activate_request_locked() = 0;
    virtual void queue_request_locked() = 0;
    virtual void load_queued_request_locked() = 0;
    virtual void on_cancel_pending_backend() = 0;
    virtual void on_preferences_changed_backend() = 0;
    virtual void on_worker_started_locked() = 0;
    virtual void on_request_queued() = 0;
    virtual void on_finish_locked(bool &backend_action) = 0;
    virtual void after_finish_unlocked(bool backend_action) = 0;
    virtual void poll_running() = 0;
};

struct autocomp_lan_ai : autocomp_ai
{
    bool build_request();
    bool run_active_request(ai_result &res);
    void activate_request_locked();
    void queue_request_locked();
    void load_queued_request_locked();
    void on_cancel_pending_backend() {}
    void on_preferences_changed_backend() {}
    void on_worker_started_locked() {}
    void on_request_queued() {}
    void on_finish_locked(bool &backend_action) {}
    void after_finish_unlocked(bool backend_action) {}
    void poll_running() {}
};

struct autocomp_emb_ai : autocomp_ai
{
    emb_ai engine;
    bool unload_requested = false;

    ~autocomp_emb_ai();

    bool build_request();
    bool run_active_request(ai_result &res);
    void activate_request_locked();
    void queue_request_locked();
    void load_queued_request_locked();
    void on_cancel_pending_backend();
    void on_preferences_changed_backend();
    void on_worker_started_locked();
    void on_request_queued();
    void on_finish_locked(bool &backend_action);
    void after_finish_unlocked(bool backend_action);
    void poll_running();
};

struct mkv_edge
{
    int to, cnt, sep_cnt;
    std::string sep;
};

struct mkv_path
{
    int tok;
    int atoms;
    double score;
    std::string text;
};

struct autocomp_markov : autocomp
{
    std::vector<std::string> toks;
    std::vector<int> freq; // TODO: these three are used in conjunction - better to re-use an existing struct to store
    std::vector<int> start_cnt;
    std::vector<int> out_cnt;
    std::vector<std::vector<mkv_edge> > edges;
    std::map<std::string, int> tok_id;
    bool dirty = true;

    void on_preferences_changed();
    void on_text_changed(int pos, int inserted, int deleted, const char* deleted_text);
    bool complete(std::string &text, int &anchor_pos);
    bool move_suggestion(int dir);
    void reset_state();
    void rebuild();
};
