#pragma once

#include <pthread.h>
#include <string>
#include <vector>
#include "ligguf_tq/runtime.h"

struct emb_ai_request
{
    int serial;
    int anchor_pos;
    std::string model_path;
    std::string prefix;
    std::string suffix;
    int max_chars;
    int context_length;
    int top_k;
    double top_p;
    double temperature;
    bool cache_prompt;
};

struct emb_ai_result
{
    std::string text;
    std::string error;
};

class emb_ai
{
public:
    emb_ai();
    ~emb_ai();

    bool complete(const emb_ai_request &req, emb_ai_result &res);
    void request_stop();
    void clear_cache();
    void unload();
    void get_partial_text(std::string &text);

private:
    bool initialized = false;
    bool loaded = false;
    volatile bool stop_requested = false;
    int loaded_context = 0;
    std::string loaded_model_path;
    std::vector<int> cached_toks;
    std::string partial_text;
    model_state state, base, cached;
    bool cache_valid = false;
    pthread_mutex_t partial_lock;

    bool ensure_model(const emb_ai_request &req, std::string &error);
    bool feed_toks(const std::vector<int> &toks, int start, std::string &error);
    void set_partial_text(const std::string &text);
};
