#ifndef LATEA_AUTOCOMPLETE_H
#define LATEA_AUTOCOMPLETE_H

#include "llama_client.h"

#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class editor_window;

class autocomplete_engine {
public:
    autocomplete_engine();
    ~autocomplete_engine();

    void set_owner(editor_window *window);
    void schedule();
    void trigger_now();
    void cancel_visible();
    void on_text_changed();
    void on_cursor_changed();
    void on_preferences_changed();
    void handle_awake();

private:
    editor_window *owner;
    std::vector<std::string> dictionary_words;
    std::string loaded_dictionary_path;
    int latest_serial;

    std::mutex worker_mutex;
    std::condition_variable worker_cv;
    std::thread worker_thread;
    bool worker_running;
    bool has_pending;
    ai_request pending_request;
    bool has_result;
    ai_result ready_result;
    llama_client client;

    static void timeout_cb(void *data);
    static void awake_cb(void *data);
    void ensure_worker();
    void worker_main();
    void request_ai_now();
    void publish_local_result(const std::string &suffix);
    std::string complete_dictionary_file();
    std::string complete_current_file();
    std::string current_word_prefix() const;
    std::string find_completion_from_words(const std::vector<std::string> &words,
        const std::string &prefix) const;
    void load_dictionary_if_needed();
};

#endif
