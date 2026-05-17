#pragma once

#include <pthread.h>
#include <string>
#include <vector>
#include "llama_client.h"
#include "ligguf_tq/runtime.h"

struct emb_ai
{
    bool initialized = false;
    bool loaded = false;
    volatile bool stop_requested = false;
    int loaded_context = 0;
    std::string loaded_model_path;
    std::vector<int> cached_toks;
    std::string partial_text;
    int used_toks = 0;
    int used_ctx = 0;
    model_state state, base, cached;
    bool cache_valid = false;
    pthread_mutex_t partial_lock;

    emb_ai();
    ~emb_ai();

    bool complete(const ai_request &req, ai_result &res);
    void request_stop();
    void clear_cache();
    void unload();
    void get_partial_text(std::string &text);
    void get_usage(int &used, int &ctx);
    bool ensure_model(const ai_request &req, std::string &error);
    bool feed_toks(const std::vector<int> &toks, int start, std::string &error);
    void set_partial_text(const std::string &text);
    void set_usage(int used, int ctx);
};
