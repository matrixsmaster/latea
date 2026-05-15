#pragma once

#include <string>
#include <vector>
#include <pthread.h>
#include "emb_ai.h"
#include "llama_client.h"

#define AUTOCOMPLETE_DELAY_SEC 0.18
#define AUTOCOMPLETE_POLL_SEC 0.05

class autocomp
{
public:
    autocomp() {}
    virtual ~autocomp();

    virtual void on_preferences_changed();
    virtual void on_text_changed(int pos, int inserted, int deleted, const char* deleted_text);
    virtual void on_cursor_changed();
    virtual void schedule();
    virtual void trigger_now();
    virtual void cancel_pending();
    virtual bool is_busy() const { return false; }

protected:
    double due_time = 0;
    bool timer_active = false;

    virtual bool complete(std::string &text, int &anchor_pos) = 0;
    bool can_complete() const;
    bool current_word(std::string &prefix, int &anchor_pos) const;
    void publish(const std::string &text, int anchor_pos, int request_id = 0);
    void cancel_timer();
    static void timer_cb(void *data);
};

class autocomp_dict : public autocomp
{
public:
    void on_preferences_changed();

protected:
    bool complete(std::string &text, int &anchor_pos);

private:
    std::string loaded_path;
    std::vector<std::string> words;
};

class autocomp_file : public autocomp
{
protected:
    bool complete(std::string &text, int &anchor_pos);
};

class autocomp_ai : public autocomp
{
public:
    autocomp_ai();
    virtual ~autocomp_ai();
    void on_preferences_changed();
    void trigger_now();
    void cancel_pending();
    bool is_busy() const;

protected:
    bool complete(std::string &text, int &anchor_pos);
    bool build_request_text(std::string &prefix, std::string &suffix, int &anchor_pos);
    int next_request_serial();
    virtual bool build_request() = 0;
    virtual bool run_active_request(ai_result &res) = 0;
    virtual const char* ai_label() const = 0;

private:
    pthread_mutex_t lock;
    pthread_t worker;
    ai_result finished_res;
    bool worker_running;
    bool worker_done;
    bool worker_ok;
    bool worker_joined;
    bool poll_active;
    bool have_queued;
    int next_serial;
    int latest_serial;

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

    void start_worker();
    void queue_or_start();
    void finish_worker();

    static void* worker_main(void* data);
    static void poll_cb(void* data);

protected:
    ai_result finished_result() const { return finished_res; }
    bool busy_running() const { return worker_running; }
    bool busy_queued() const { return have_queued; }
    int current_serial() const { return latest_serial; }
    pthread_mutex_t* mutex() { return &lock; }
};

class autocomp_lan_ai : public autocomp_ai
{
protected:
    bool build_request();
    bool run_active_request(ai_result &res);
    const char* ai_label() const;

private:
    ai_request pending_req;
    ai_request active_req;
    ai_request queued_req;
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

class autocomp_emb_ai : public autocomp_ai
{
public:
    autocomp_emb_ai() {}
    ~autocomp_emb_ai();

protected:
    bool build_request();
    bool run_active_request(ai_result &res);
    const char* ai_label() const;

private:
    emb_ai engine;
    emb_ai_request pending_req;
    emb_ai_request active_req;
    emb_ai_request queued_req;
    bool unload_requested = false;
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
