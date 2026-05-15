/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <map>

// Base constants
#define ALIGNMENT 32
#define MAXTENSORS 4000
#define MAXMETAKV 500
#define MAXNAMELEN 1024
#define QK8_0 32
#define QK_K 256
#define QK_TQ 128
#define QK_TQ_QS3 (QK_TQ * 3 / 8)
#define K_SCALE_SIZE 12

// Standard GGUF keys
#define TOKENS_KEY "tokenizer.ggml.tokens"
#define TOKENS_SCORE_KEY "tokenizer.ggml.scores"
#define TOKENS_TYPE_KEY "tokenizer.ggml.token_type"
#define TOKENS_MERGES_KEY "tokenizer.ggml.merges"
#define TOKENS_MODEL_KEY "tokenizer.ggml.model"
#define TOKENS_PRE_KEY "tokenizer.ggml.pre"
#define TOKENS_EMBED_KEY "token_embd.weight"
#define TOKENS_BOS_KEY "tokenizer.ggml.bos_token_id"
#define TOKENS_EOS_KEY "tokenizer.ggml.eos_token_id"
#define TOKENS_UNK_KEY "tokenizer.ggml.unknown_token_id"
#define TOKENS_SEP_KEY "tokenizer.ggml.seperator_token_id"
#define TOKENS_PAD_KEY "tokenizer.ggml.padding_token_id"
#define TOKENS_CLS_KEY "tokenizer.ggml.cls_token_id"
#define TOKENS_MASK_KEY "tokenizer.ggml.mask_token_id"
#define TOKENS_ADD_BOS_KEY "tokenizer.ggml.add_bos_token"
#define ARCH_KEY "general.architecture"
#define OUTPUT_KEY "output.weight"
#define OUTPUT_NORM_KEY "output_norm.weight"

// LLaMA-specific keys
#define VOCAB_SIZE_KEY "llama.vocab_size"
#define CONTEXT_LEN_KEY "llama.context_length"
#define EMBED_LEN_KEY "llama.embedding_length"
#define HEAD_COUNT_KEY "llama.attention.head_count"
#define HEAD_KV_COUNT_KEY "llama.attention.head_count_kv"
#define BLOCK_COUNT_KEY "llama.block_count"
#define RMS_EPSILON_KEY "llama.attention.layer_norm_rms_epsilon"
#define ROPE_BASE_KEY "llama.rope.freq_base"
#define ROPE_DIMS_KEY "llama.rope.dimension_count"
#define FF_LEN_KEY "llama.feed_forward_length"

// Qwen3-specific keys
#define QWEN3_BLOCK_COUNT_KEY "qwen3.block_count"
#define QWEN3_CONTEXT_LEN_KEY "qwen3.context_length"
#define QWEN3_EMBED_LEN_KEY "qwen3.embedding_length"
#define QWEN3_HEAD_COUNT_KEY "qwen3.attention.head_count"
#define QWEN3_HEAD_KV_COUNT_KEY "qwen3.attention.head_count_kv"
#define QWEN3_RMS_EPSILON_KEY "qwen3.attention.layer_norm_rms_epsilon"
#define QWEN3_ROPE_BASE_KEY "qwen3.rope.freq_base"
#define QWEN3_ROPE_SCALING_TYPE_KEY "qwen3.rope.scaling.type"
#define QWEN3_ROPE_SCALING_FACTOR_KEY "qwen3.rope.scaling.factor"
#define QWEN3_ROPE_SCALING_ORIG_CTX_KEY "qwen3.rope.scaling.original_context_length"
#define QWEN3_KEY_LEN_KEY "qwen3.attention.key_length"
#define QWEN3_VALUE_LEN_KEY "qwen3.attention.value_length"
#define QWEN3_FF_LEN_KEY "qwen3.feed_forward_length"

//BERT-specific keys
#define BERT_BLOCK_COUNT_KEY "bert.block_count"
#define BERT_CONTEXT_LEN_KEY "bert.context_length"
#define BERT_EMBED_LEN_KEY "bert.embedding_length"
#define BERT_HEAD_COUNT_KEY "bert.attention.head_count"
#define BERT_LN_EPSILON_KEY "bert.attention.layer_norm_epsilon"
#define BERT_FF_LEN_KEY "bert.feed_forward_length"
#define BERT_POS_EMBED_KEY "position_embd.weight"
#define BERT_TYPE_EMBED_KEY "token_types.weight"
#define BERT_EMB_NORM_W_KEY "token_embd_norm.weight"
#define BERT_EMB_NORM_B_KEY "token_embd_norm.bias"

enum gguf_type {
    F32 = 0, F16 = 1,
    Q4_0 = 2, Q4_1 = 3, Q4_2 = 4, Q4_3 = 5,
    Q5_0 = 6, Q5_1 = 7,
    Q8_0 = 8, Q8_1 = 9,
    Q2_K = 10, Q3_K = 11, Q4_K = 12, Q5_K = 13, Q6_K = 14, Q8_K = 15,
    IQ2_XXS = 16, IQ2_XS = 17, IQ3_XXS = 18,
    IQ1_S = 19, IQ4_NL = 20, IQ3_S = 21, IQ2_S = 22, IQ4_XS = 23,
    I8 = 24, I16 = 25, I32 = 26, I64 = 27,
    F64 = 28,
    IQ1_M = 29,
    BF16 = 30,
    MXFP4 = 39,
    // Custom types
    Q1_0 = 40,
    Q1_G = 41, // previously Q1_0_G128
    GGUF_TYPE_COUNT
};

enum gguf_val_type {
    GUINT8, GINT8, GUINT16, GINT16, GUINT32, GINT32,
    GFLOAT32, GBOOL, GSTRING, GARRAY, GUINT64, GINT64, GFLOAT64,
    GGUF_VAL_TYPE_COUNT
};

typedef uint16_t gguf_half;

struct gguf_kv {
    uint64_t off;
    gguf_val_type tag;
};

struct gguf_tensor {
    uint64_t off;
    std::vector<uint64_t> dims;
    gguf_type type;
};

struct block_q8_0 {
    uint16_t d;
    int8_t qs[QK8_0];
};

struct block_q1_0 {
    gguf_half d;
    uint8_t qs[QK8_0/8];
};

struct block_q1_G {
    gguf_half d;
    uint8_t qs[128/8];
};

struct block_q2_K {
    uint8_t scales[QK_K/16];
    uint8_t qs[QK_K/4];
    gguf_half d;
    gguf_half dmin;
};

struct block_q3_K {
    uint8_t hmask[QK_K/8];
    uint8_t qs[QK_K/4];
    uint8_t scales[12];
    gguf_half d;
};

struct block_q4_K {
    gguf_half d;
    gguf_half dmin;
    uint8_t scales[K_SCALE_SIZE];
    uint8_t qs[QK_K/2];
};

struct block_q5_K {
    gguf_half d;
    gguf_half dmin;
    uint8_t scales[K_SCALE_SIZE];
    uint8_t qh[QK_K/8];
    uint8_t qs[QK_K/2];
};

struct block_q6_K {
    uint8_t ql[QK_K/2];
    uint8_t qh[QK_K/4];
    int8_t scales[QK_K/16];
    gguf_half d;
};

struct block_q8_K {
    float d;
    int8_t qs[QK_K];
    int16_t bsums[QK_K/16];
};

struct block_tq_k {
    gguf_half d;
    gguf_half rd;
    uint8_t qs[QK_TQ_QS3];
    uint8_t signs[QK_TQ/8];
};

struct block_tq_v {
    gguf_half d;
    uint8_t qs[QK_TQ_QS3];
};

typedef std::vector<block_q8_0> qtensor;
typedef std::vector<block_q8_K> ktensor;
typedef std::vector<gguf_half> htensor;
typedef std::vector<float> ftensor;
typedef std::vector<block_tq_k> tqtensor_k;
typedef std::vector<block_tq_v> tqtensor_v;

struct wtensor {
    uint8_t* ptr = NULL;
    gguf_type type = GGUF_TYPE_COUNT;
    uint32_t rsz = 0;
};

struct trans_block {
    float* att_norm;
    wtensor att_q;
    wtensor att_k;
    wtensor att_v;
    wtensor att_out;
    float* att_q_norm;
    float* att_k_norm;
    float* ffn_norm;
    wtensor ffn_up;
    wtensor ffn_down;
    wtensor ffn_gate;
};

struct bert_block {
    wtensor attn_q, attn_k, attn_v, attn_out;
    float* attn_q_bias;
    float* attn_k_bias;
    float* attn_v_bias;
    float* attn_out_bias;
    float* attn_norm_w;
    float* attn_norm_b;
    wtensor ffn_up, ffn_down;
    float* ffn_up_bias;
    float* ffn_down_bias;
    float* ffn_norm_w;
    float* ffn_norm_b;
};

struct gguf_model {
    int file = -1; // model file handle
    uint8_t* base = NULL; // base mmap address
    uint64_t fsize = 0; // model file size
    uint8_t* tensors_off = NULL; // aligned offset of the start of tensors block
    std::string arch; // model architecture

    int vocab_size; // size of the vocabulary
    int n_layers; // number of layers
    int n_heads; // number of Query heads
    int n_kv_heads; // number of Key/Value heads
    int n_embed; // input embedding size
    int n_context; // size of the context window
    int tok_bos, tok_eos; // BOS/EOS token IDs
    int tok_unk, tok_sep, tok_cls; // extra tokenizer IDs
    bool add_bos = true; // tokenizer BOS policy
    int head_dim; // head size (computed)
    float rms_epsilon; // epsilon for RMS norm
    float rope_base; // RoPE base frequency
    int rope_dim; // RoPE dimension, clamped to head_dim during inference
    std::string rope_scaling;
    float rope_scale = 1.0f;
    int rope_orig_context = 0;
    int n_ff; // feed-forward hidden size
    int q_dim; // total Query width across heads
    int kv_dim; // total K/V width across KV heads

    std::vector<std::string> tokens; // vector of known tokens (pos == index)
    std::map<std::string,int> tokens_rev; // reverse token lookup by string
    std::vector<std::string> special_tokens; // tokenizer control tokens matched before BPE
    ftensor tokscores; // tokenizer scores from GGUF
    std::vector<std::string> merges;
    std::map<std::string,int> merge_rank;
    std::string tok_model, tok_pre; // tokenizer model string and pretokenizer type
    std::map<std::string,gguf_kv> meta_kv; // model metadata key/value pairs
    std::map<std::string,gguf_tensor> tensors; // all tensors described in the GGUF
    wtensor t_embed; // token embedding tensor (const)
    wtensor t_out; // output classifier tensor (const)
    float* t_outnorm; // final RMS norm weights (const)
    std::vector<trans_block> tr; // transformer blocks/layers (const)
    wtensor bert_tok_embd; // BERT token embedding tensor (const)
    wtensor bert_pos_embd; // BERT position embedding tensor (const)
    wtensor bert_tok_types; // BERT token-type embedding tensor (const)
    float* bert_emb_norm_w = NULL; // BERT embedding layernorm weights (const)
    float* bert_emb_norm_b = NULL; // BERT embedding layernorm bias (const)
    std::vector<bert_block> bert; // BERT encoder blocks/layers (const)

    uint32_t kvrd32(const std::string& key) const;
    float kvrdf32(const std::string& key) const;
    std::string kvrdstr(const std::string& key) const;
    bool kvrdbool(const std::string& key) const;

    bool open_mmap(const char* fn);
    void close_mmap();
    bool read_tokenizer();
    bool read_gguf();
    bool read_llama();
    bool read_qwen3();
    bool read_bert();
};

uint64_t row_size(gguf_type type, int len);
