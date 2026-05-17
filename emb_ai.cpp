#include <cstdlib>
#include <ctime>
#include <vector>
#include "common.h"
#include "emb_ai.h"

using namespace std;

emb_ai::emb_ai()
{
    pthread_mutex_init(&partial_lock, NULL);
}

emb_ai::~emb_ai()
{
    unload();
    pthread_mutex_destroy(&partial_lock);
}

bool emb_ai::complete(const ai_request &req, ai_result &res)
{
    int ntoks = 0;
    int shared = 0;
    vector<int> toks;

    res.text.clear();
    res.error.clear();
    stop_requested = false;
    set_partial_text("");
    if (!ensure_model(req, res.error)) return false;

    toks = tokenize(&base, req.prefix.c_str(), true, false);
    if (req.cache_prompt && cache_valid) {
        while (shared < (int)toks.size() && shared < (int)cached_toks.size() && toks[shared] == cached_toks[shared]) shared++;
        state = cached;
        state.pos = shared;
        if (!feed_toks(toks, shared, res.error)) return false;
    } else {
        state = base;
        if (!feed_toks(toks, 0, res.error)) return false;
    }
    set_usage(state.pos, state.m.n_context);

    if (req.cache_prompt) {
        cached_toks = toks;
        cached = state;
        cache_valid = true;
    } else {
        cached_toks.clear();
        cache_valid = false;
    }

    state.topk = req.top_k;
    state.topp = req.top_p;
    state.temp = req.temperature;
    state.greedy = req.temperature <= 0.0;

    while (ntoks < req.max_chars) {
        if (stop_requested) {
            res.error = "Embedded AI stopped";
            return false;
        }

        int tok = state.greedy ? sampler_greedy(&state) : sampler_topp(&state);
        if (!tok || tok == state.m.tok_eos) break;

        string piece = tok_to_str(&state, tok);
        string prev = sanitize_suggestion(res.text, req.max_chars);

        res.text += piece;
        if (sanitize_suggestion(res.text, req.max_chars) == prev) break;

        set_partial_text(res.text);
        inference(&state,tok);
        set_usage(state.pos, state.m.n_context);
        ntoks++;
    }
    return true;
}

void emb_ai::request_stop()
{
    stop_requested = true;
}

void emb_ai::clear_cache()
{
    cached_toks.clear();
    cache_valid = false;
}

void emb_ai::unload()
{
    if (loaded) state.m.close_mmap();
    loaded = false;
    loaded_context = 0;
    loaded_model_path.clear();
    clear_cache();
    set_partial_text("");
    set_usage(0, 0);
}

void emb_ai::get_partial_text(std::string &text)
{
    pthread_mutex_lock(&partial_lock);
    text = partial_text;
    pthread_mutex_unlock(&partial_lock);
}

void emb_ai::get_usage(int &used, int &ctx)
{
    pthread_mutex_lock(&partial_lock);
    used = used_toks;
    ctx = used_ctx;
    pthread_mutex_unlock(&partial_lock);
}

bool emb_ai::ensure_model(const ai_request &req, string &error)
{
    if (loaded) return true;

    if (req.model_path.empty()) {
        error = "Embedded AI model path is empty";
        return false;
    }

    if (!initialized) {
        srand(time(NULL));
        fp1632_init();
        tq_init();
        initialized = true;
    }

    state = model_state();
    state.model_fn = req.model_path;
    state.u_context = req.context_length;
    state.tq = req.tquant;
    if (!state.m.open_mmap(state.model_fn.c_str())) {
        error = "Embedded AI cannot open model";
        return false;
    }
    if (!state.m.read_gguf()) {
        error = "Embedded AI cannot read GGUF";
        state.m.close_mmap();
        return false;
    }
    state.allocate();
    if (!state.m.read_tokenizer()) {
        error = "Embedded AI cannot read tokenizer";
        state.m.close_mmap();
        return false;
    }

    base = state;
    cached = state;
    cache_valid = false;
    loaded_model_path = req.model_path;
    loaded_context = req.context_length;
    loaded = true;

    return true;
}

bool emb_ai::feed_toks(const vector<int> &toks, int start, string &error)
{
    if (state.pos + (int)toks.size() - start >= state.m.n_context) {
        error = "Embedded AI prompt does not fit into context";
        return false;
    }

    for (int i = start; i < (int)toks.size(); i++) {
        int tok = toks[i];

        if (stop_requested) {
            error = "Embedded AI stopped";
            return false;
        }

        if (!tok || tok == state.m.tok_eos) break;
        inference(&state,tok);
    }
    return true;
}

void emb_ai::set_partial_text(const std::string &text)
{
    pthread_mutex_lock(&partial_lock);
    partial_text = text;
    pthread_mutex_unlock(&partial_lock);
}

void emb_ai::set_usage(int used, int ctx)
{
    pthread_mutex_lock(&partial_lock);
    used_toks = used;
    used_ctx = ctx;
    pthread_mutex_unlock(&partial_lock);
}
