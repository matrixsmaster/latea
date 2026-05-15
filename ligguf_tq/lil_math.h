/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#pragma once

#include "lil_gguf.h"

// The paper requires random rotations / sketches, but it does not prescribe concrete seeds.
// These seeds are local deterministic choices so repeated runs and save/load give the same cache.
// `TQ_ROT_SEED` initializes the paper's random orthogonal matrix. We instantiate it once with a Gaussian matrix and deterministic Gram-Schmidt.
#define TQ_ROT_SEED 0x2c1b3c6dU  // Seed for the MSE-stage random orthogonal rotation.
#define TQ_QJL_SEED 0x91e10da5U  // Independent seed for the residual sign sketch.
// These hash constants are only used to synthesize deterministic pseudo-random signs / Gaussian draws from (seed,index).
#define TQ_HASH_MUL0 0x7feb352dU
#define TQ_HASH_MUL1 0x846ca68bU
#define TQ_HASH_GOLDEN 0x9e3779b1U
// `TQ_ROT_STAGE2` is only used by the non-128 fallback path, where we keep the older SRHT-style transform for dimensions other than `QK_TQ`.
#define TQ_ROT_STAGE2 0xa511e9b3U  // Second independent diagonal for the fallback Hadamard rotation.
#define TQ_QJL_SCALE 1.2533141373155003f  // sqrt(pi / 2), the dequantization factor in the QJL estimator.
#define TQ_CB_GRID_SIZE 20001  // Numerical integration grid for startup Lloyd-Max fitting; local solver setting, not a paper constant.
#define TQ_CB_MAX_ITERS 128  // Iteration cap for the startup Lloyd-Max solve; high enough to converge, bounded to keep startup finite.
#define TQ_CB_EPS 1e-12  // Convergence threshold for the startup Lloyd-Max solve.
#define TQ_HASH_U24_DENOM 16777216.0f  // 2^24 from the high bits of the hash, used to build Box-Muller uniforms.
#define TQ_HASH_U24_DENOM_OPEN 16777217.0f  // 2^24 + 1 keeps the first Box-Muller uniform strictly above zero.
#define TQ_TAU 6.2831853071795865f  // 2 * pi, the period of the Box-Muller angle.
#define TQ_BLOCK_INV_SQRT 0.08838834764831843f  // 1 / sqrt(QK_TQ), used in the fixed 128-wide hot path.
#define TQ_QJL_BLOCK_SCALE 0.009791516697244164f  // TQ_QJL_SCALE / QK_TQ for the same fixed-size hot path.
#define GELU_TANH_SCALE 0.7978845608f  // sqrt(2 / pi) in the tanh GELU approximation.
#define GELU_TANH_CUBIC 0.044715f  // Cubic correction term in the tanh GELU approximation.

#define fp16_to_fp32(H) (fp1632_lut[H])

extern float fp1632_lut[65536];

void f32_to_f16_row(const float* x, gguf_half* y, int n);

void fp1632_init();

void dequantize_row_q8_0(const block_q8_0* x, float* y, int64_t k);
void dequantize_row_q1_0(const block_q1_0* x, float* y, int64_t k);
void dequantize_row_q1_G(const block_q1_G* x, float* y, int64_t k);
void dequantize_row_q2_K(const block_q2_K* x, float* y, int64_t k);
void dequantize_row_q3_K(const block_q3_K* x, float* y, int64_t k);
void dequantize_row_q4_K(const block_q4_K* x, float* y, int64_t k);
void dequantize_row_q5_K(const block_q5_K* x, float* y, int64_t k);
void dequantize_row_q6_K(const block_q6_K* x, float* y, int64_t k);
void dequantize_row(gguf_type t, const void* in, float* out, int64_t n);

float vec_dot_f32(const float* a, const float* b, int size);
float vec_dot_f16_f32(const gguf_half* a, const float* b, int size);

void quantize_q8_0(qtensor &y, const ftensor &x);
void quantize_q8_K(ktensor &y, const ftensor &x);

void tq_init();
void tq_rotate(float* dst, const float* src, int n);
void quantize_tq_key_row(const float* x, block_tq_k* y, int n);
void quantize_tq_val_row(const float* x, block_tq_v* y, int n);
void prep_tq_query(const float* q, float* q0, float* q1, int n);
void tq_restore(float* dst, const float* src, int n);
float vec_dot_tq_key_query(const block_tq_k* k, const float* q0, const float* q1, int n);
void vec_madd_tq_val(float* dst, const block_tq_v* v, float a, int n);

void matmul(ftensor &out, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w, int n, int d);
void matmul2(ftensor &out0, ftensor &out1, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w0, wtensor w1, int n, int d);
void matmul3(ftensor &out0, ftensor &out1, ftensor &out2, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w0, wtensor w1, wtensor w2, int n, int d0, int d1, int d2);

void rope(ftensor& x, int n_heads, int pos, const gguf_model &mdl, const ftensor &rope_freq);
void rmsnorm(ftensor &out, const ftensor &x, float* w, int size, float epsilon);
void rmsnorm_inplace(float* x, float* w, int size, float epsilon);
void layernorm(ftensor &out, const ftensor &x, const float* w, const float* b, int size, float epsilon);
void l2norm(ftensor &x);
void softmax(float* x, int size);
void gelu(ftensor &x);
