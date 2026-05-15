/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "lil_gguf.h"
#include "lil_math.h"
#include "runtime.h"

using namespace std;

void inference(model_state* S, int tok)
{
    const float att_scale = 1.0f / sqrtf(S->m.head_dim);
    const int pos = S->pos++;

    dequantize_row(S->m.t_embed.type, S->m.t_embed.ptr + tok * S->m.t_embed.rsz, S->x.data(), S->m.n_embed);

    for (int l = 0; l < S->m.n_layers; l++) {
        rmsnorm(S->xb, S->x, S->m.tr[l].att_norm, S->m.n_embed, S->m.rms_epsilon);

        quantize_q8_K(S->xq, S->xb);
        matmul3(S->q, S->k, S->v, &S->xb, &S->xq, NULL, S->m.tr[l].att_q, S->m.tr[l].att_k, S->m.tr[l].att_v, S->m.n_embed, S->m.q_dim, S->m.kv_dim, S->m.kv_dim);

        if (S->m.arch == "qwen3") {
            float* p = S->q.data();
            for (int h = 0; h < S->m.n_heads; h++, p += S->m.head_dim)
                rmsnorm_inplace(p, S->m.tr[l].att_q_norm, S->m.head_dim, S->m.rms_epsilon);

            p = S->k.data();
            for (int h = 0; h < S->m.n_kv_heads; h++, p += S->m.head_dim)
                rmsnorm_inplace(p, S->m.tr[l].att_k_norm, S->m.head_dim, S->m.rms_epsilon);
        }

        rope(S->q, S->m.n_heads, pos, S->m, S->rope_freq);
        rope(S->k, S->m.n_kv_heads, pos, S->m, S->rope_freq);

        if (S->tq) {
            size_t loff = (size_t)l * S->m.n_context * S->tq_row_blocks;
            block_tq_k* kc_row = S->tq_kc.data() + loff + pos * S->tq_row_blocks;
            block_tq_v* vc_row = S->tq_vc.data() + loff + pos * S->tq_row_blocks;
            for (int h = 0; h < S->m.n_kv_heads; h++) {
                quantize_tq_key_row(S->k.data() + h * S->m.head_dim, kc_row + h * S->tq_head_blocks, S->m.head_dim);
                quantize_tq_val_row(S->v.data() + h * S->m.head_dim, vc_row + h * S->tq_head_blocks, S->m.head_dim);
                tq_rotate(S->tq_vcur.data() + h * S->m.head_dim, S->v.data() + h * S->m.head_dim, S->m.head_dim);
            }
        } else {
            size_t loff = (size_t)l * S->m.n_context * S->m.kv_dim;
            gguf_half* kc_row = S->kc.data() + loff + pos * S->m.kv_dim;
            gguf_half* vc_row = S->vc.data() + loff + pos * S->m.kv_dim;
            f32_to_f16_row(S->k.data(), kc_row, S->m.kv_dim);
            f32_to_f16_row(S->v.data(), vc_row, S->m.kv_dim);
        }

        const int nrep = S->m.n_heads / S->m.n_kv_heads;

        MULTITHREAD
        for (int h = 0; h < S->m.n_heads; h++) {
            float* q = S->q.data() + (h * S->m.head_dim);
            float* k = S->k.data() + (h / nrep) * S->m.head_dim;
            float* xb = S->xb.data() + (h * S->m.head_dim);

            if (S->tq) {
                size_t loff = (size_t)l * S->m.n_context * S->tq_row_blocks;
                float* tq_q = S->tq_q.data() + h * S->m.head_dim;
                float* tq_q2 = S->tq_q2.data() + h * S->m.head_dim;
                float* tq_vcur = S->tq_vcur.data() + (h / nrep) * S->m.head_dim;
                float* tq_v2 = S->tq_v2.data() + h * S->m.head_dim;
                prep_tq_query(q, tq_q, tq_q2, S->m.head_dim);

                memset(tq_v2, 0, S->m.head_dim * sizeof(float));
                float amax = -INFINITY;
                float asum = 0.0f;

                for (int t = 0; t <= pos; t++) {
                    float score;
                    if (t == pos) score = vec_dot_f32(q, k, S->m.head_dim);
                    else {
                        block_tq_k* kc_hist = S->tq_kc.data() + loff + t * S->tq_row_blocks + (h / nrep) * S->tq_head_blocks;
                        score = vec_dot_tq_key_query(kc_hist, tq_q, tq_q2, S->m.head_dim);
                    }
                    score *= att_scale;

                    if (score > amax) {
                        float scale = expf(amax - score);
                        for (int i = 0; i < S->m.head_dim; i++) tq_v2[i] *= scale;
                        asum *= scale;
                        amax = score;
                    }

                    float a = expf(score - amax);
                    asum += a;
                    if (t == pos) {
                        for (int i = 0; i < S->m.head_dim; i++) tq_v2[i] += a * tq_vcur[i];
                    } else {
                        block_tq_v* vc_hist = S->tq_vc.data() + loff + t * S->tq_row_blocks + (h / nrep) * S->tq_head_blocks;
                        vec_madd_tq_val(tq_v2, vc_hist, a, S->m.head_dim);
                    }
                }

                float isum = 1.0f / asum;
                for (int i = 0; i < S->m.head_dim; i++) tq_v2[i] *= isum;
                tq_restore(xb, tq_v2, S->m.head_dim);
            } else {
                size_t loff = (size_t)l * S->m.n_context * S->m.kv_dim;
                float* v = S->v.data() + (h / nrep) * S->m.head_dim;

                memset(xb, 0, S->m.head_dim * sizeof(float));
                float amax = -INFINITY;
                float asum = 0.0f;

                for (int t = 0; t <= pos; t++) {
                    float score;
                    if (t == pos) score = vec_dot_f32(q, k, S->m.head_dim);
                    else {
                        gguf_half* kc_hist = S->kc.data() + loff + t * S->m.kv_dim + (h / nrep) * S->m.head_dim;
                        score = vec_dot_f16_f32(kc_hist, q, S->m.head_dim);
                    }
                    score *= att_scale;

                    if (score > amax) {
                        float scale = expf(amax - score);
                        for (int i = 0; i < S->m.head_dim; i++) xb[i] *= scale;
                        asum *= scale;
                        amax = score;
                    }

                    float a = expf(score - amax);
                    asum += a;
                    if (t == pos) {
                        for (int i = 0; i < S->m.head_dim; i++) xb[i] += a * v[i];
                    } else {
                        gguf_half* vc_hist = S->vc.data() + loff + t * S->m.kv_dim + (h / nrep) * S->m.head_dim;
                        for (int i = 0; i < S->m.head_dim; i++) xb[i] += a * fp16_to_fp32(vc_hist[i]);
                    }
                }

                float isum = 1.0f / asum;
                for (int i = 0; i < S->m.head_dim; i++) xb[i] *= isum;
            }
        }

        quantize_q8_K(S->xq, S->xb);
        matmul(S->xb2, &S->xb, &S->xq, NULL, S->m.tr[l].att_out, S->m.q_dim, S->m.n_embed);
        for (int j = 0; j < S->m.n_embed; j++) S->x[j] += S->xb2[j];

        rmsnorm(S->xb, S->x, S->m.tr[l].ffn_norm, S->m.n_embed, S->m.rms_epsilon);
        quantize_q8_K(S->xq, S->xb);
        matmul2(S->hb, S->hb2, &S->xb, &S->xq, NULL, S->m.tr[l].ffn_up, S->m.tr[l].ffn_gate, S->m.n_embed, S->m.n_ff);

        for (int i = 0; i < S->m.n_ff; i++) {
            const float g = S->hb2[i];
            S->hb[i] = (g / (1.0f + expf(-g))) * S->hb[i];
        }

        quantize_q8_K(S->xq, S->hb);
        matmul(S->xb, &S->hb, &S->xq, NULL, S->m.tr[l].ffn_down, S->m.n_ff, S->m.n_embed);
        for (int j = 0; j < S->m.n_embed; j++) S->x[j] += S->xb[j];
    }

    rmsnorm(S->x, S->x, S->m.t_outnorm, S->m.n_embed, S->m.rms_epsilon);
    if (S->m.t_out.type == Q8_0 || S->m.t_out.type == Q1_0 || S->m.t_out.type == Q1_G) {
        quantize_q8_0(S->xq8, S->x);
        matmul(S->logits, &S->x, NULL, &S->xq8, S->m.t_out, S->m.n_embed, S->m.vocab_size);
    } else {
        quantize_q8_K(S->xq, S->x);
        matmul(S->logits, &S->x, &S->xq, NULL, S->m.t_out, S->m.n_embed, S->m.vocab_size);
    }
}

int sampler_greedy(model_state* S)
{
    if (S->logits.empty()) return S->m.tok_eos;

    int best_id = 0;
    float best_v = S->logits[0];
    for (int i = 1; i < (int)S->logits.size(); i++) {
        if (S->logits[i] > best_v) {
            best_v = S->logits[i];
            best_id = i;
        }
    }

    return best_id;
}

static int cmp_sampler_ent_desc(const void* pa, const void* pb)
{
    const sampler_entry* a = (const sampler_entry*)pa;
    const sampler_entry* b = (const sampler_entry*)pb;

    if (a->p < b->p) return 1;
    if (a->p > b->p) return -1;
    return a->tok - b->tok;
}

int sampler_topp(model_state* S)
{
    const int nvoc = S->m.vocab_size;
    int n_maxlog = sampler_greedy(S);
    float maxlog = S->logits[n_maxlog];
    if (S->temp <= 0.0f) return n_maxlog;
    float inv_temp = 1.0f / S->temp;

    float sum = 0.0f;
    for (int i = 0; i < nvoc; i++) {
        float p = expf((S->logits[i] - maxlog) * inv_temp);
        S->samp[i].p = p;
        S->samp[i].tok = i;
        sum += p;
    }
    if (sum <= 0.0f) return n_maxlog;

    for (int i = 0; i < nvoc; i++) S->samp[i].p /= sum;
    qsort(S->samp.data(), nvoc, sizeof(S->samp[0]), cmp_sampler_ent_desc);

    float top = S->topp;
    if (top <= 0.0f) top = S->samp[0].p;
    if (top > 1.0f) top = 1.0f;
    int topk = S->topk;
    if (topk <= 0 || topk > nvoc) topk = nvoc;

    float cum = 0.0f;
    int cutoff = 0;
    for (; cutoff < topk; cutoff++) {
        cum += S->samp[cutoff].p;
        if (cum >= top) break;
    }
    if (cutoff >= topk) cutoff = topk - 1;

    float r = ((float)rand() / (float)RAND_MAX) * cum;
    float pcum = 0.0f;
    for (int i = 0; i <= cutoff; i++) {
        pcum += S->samp[i].p;
        if (pcum >= r) return S->samp[i].tok;
    }

    return S->samp[cutoff].tok;
}
