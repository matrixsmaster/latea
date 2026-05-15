/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#include <time.h>
#include <string>
#include "common.h"

using namespace std;

void model_state::allocate()
{
    if (u_context) {
        if (u_context > m.n_context)
            ERR("This model doesn't support context larger than %d",m.n_context);
        else
            m.n_context = u_context;
        DBG("Model context length set to %d",m.n_context);
    }

    x.resize(m.n_embed);
    xb.resize(MAX(m.n_embed,m.q_dim));
    xb2.resize(m.n_embed);
    xq8.resize(m.n_ff / QK8_0);
    xq.resize(m.n_ff / QK_K);
    hb.resize(m.n_ff);
    hb2.resize(m.n_ff);
    q.resize(m.q_dim);
    k.resize(m.kv_dim);
    v.resize(m.kv_dim);
    if (tq) {
        tq_head_blocks = (m.head_dim + QK_TQ - 1) / QK_TQ;
        tq_row_blocks = m.n_kv_heads * tq_head_blocks;
        tq_kc.resize((size_t)m.n_layers * m.n_context * tq_row_blocks);
        tq_vc.resize(tq_kc.size());
        tq_q.resize((size_t)m.n_heads * m.head_dim);
        tq_q2.resize((size_t)m.n_heads * m.head_dim);
        tq_vcur.resize((size_t)m.n_kv_heads * m.head_dim);
        tq_v2.resize((size_t)m.n_heads * m.head_dim);
        kc.clear();
        vc.clear();
    } else {
        tq_head_blocks = 0;
        tq_row_blocks = 0;
        kc.resize((size_t)m.n_layers * m.n_context * m.kv_dim,0);
        vc.resize(kc.size(),0);
        tq_kc.clear();
        tq_vc.clear();
        tq_q.clear();
        tq_q2.clear();
        tq_vcur.clear();
        tq_v2.clear();
    }
    rope_freq.resize(m.rope_dim / 2);
    logits.resize(m.vocab_size,0);
    samp.resize(m.vocab_size);

    for (int i = 0; i < (int)rope_freq.size(); i++)
        rope_freq[i] = powf(m.rope_base, -2.0f * (float)i / (float)m.rope_dim);
}

double tmsec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

string load_text_file(const char* fn)
{
    FILE* f = fopen(fn,"r");
    if (!f) {
        ERR("Can't open file %s",fn);
        return "";
    }

    char line[MAXLINE];
    string full;
    while (!feof(f)) {
        if (!fgets(line,sizeof(line),f)) break;
        full += line;
    }

    fclose(f);
    return full;
}

bool save_text_file(const char* fn, const string &text)
{
    FILE* f = fopen(fn,"w");
    if (!f) {
        ERR("Can't open file %s",fn);
        return false;
    }
    bool ok = fwrite(text.data(),text.size(),1,f);
    fclose(f);

    if (!ok) ERR("Can't write file %s",fn);
    return ok;
}
