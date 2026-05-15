#pragma once

#include <string>
#include "lil_gguf.h"

#define DEFAULT_TOPP 0.9f
#define DEFAULT_TEMP 0.5f
#define MAXLINE 4096
#define MULTITHREAD _Pragma("omp parallel for")

// Helpful debug/error macros
#define ERR(S,...) fprintf(stderr,S "\n", __VA_ARGS__)
#ifdef DEBUG
#define DBG(S,...) printf(S "\n", __VA_ARGS__)
#else
#define DBG(...)
#endif

#define MIN(A,B) (((A) < (B))? (A) : (B))
#define MAX(A,B) (((A) > (B))? (A) : (B))

struct sampler_entry {
    float p;
    int tok;
};

struct model_file {
    int pos;
    int n_context;
    uint64_t kv_size;
    uint64_t logits_size;
    uint8_t tq;
} __attribute((packed));

struct model_state {
    std::string model_fn; // model file name
    gguf_model m; // model current state

    int u_context = 0; // user-supplied context length
    int n_gen = 0; // number of tokens to generate
    int pos = 0; // current position
    int topk = 0; // TopK sampling
    float topp = DEFAULT_TOPP; // TopP sampling
    float temp = DEFAULT_TEMP; // sampling temperature
    bool tq = false; // TurboQuant KV cache flag
    bool greedy = false; // greedy sampling flag

    ftensor x; // current activation
    qtensor xq8; // current activation quantized to Q8_0 for Q8_0 weights
    ktensor xq; // current activation quantized to Q8_K for K-quants
    ftensor xb; // residual branch buffer / attention result
    ftensor xb2; // projected attention output
    ftensor hb; // FFN up output / SwiGLU output
    ftensor hb2; // FFN gate output
    ftensor q, k, v; // current QKV vectors
    htensor kc, vc; // fp16 KV cache
    tqtensor_k tq_kc; // TurboQuant key cache
    tqtensor_v tq_vc; // TurboQuant value cache
    ftensor tq_q; // per-head rotated query scratch
    ftensor tq_q2; // per-head QJL query scratch
    ftensor tq_vcur; // current-token rotated values, one row per KV head
    ftensor tq_v2; // per-head rotated attention/value scratch
    ftensor rope_freq; // precomputed inverse RoPE frequencies
    ftensor logits; // output logits for the next token
    std::vector<sampler_entry> samp; // TopP work buffer
    int tq_head_blocks = 0; // TurboQuant blocks per KV head
    int tq_row_blocks = 0; // TurboQuant blocks in one full K/V row

    int ntimed = 0; // number of times we've timed the inference
    double gen_ms = 0; // total time length of all inference steps
    int nsampled = 0; // number of times we've timed the sampler
    double samp_ms = 0; // total time length of all sampling steps

    void allocate();
};

double tmsec();
std::string load_text_file(const char* fn);
bool save_text_file(const char* fn, const std::string &text);
