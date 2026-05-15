/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#include <assert.h>
#include "common.h"
#include "lil_math.h"

using namespace std;

float fp1632_lut[65536]; // FP16->32 conversion LUT
static qtensor m_xq8; // scratchpad

// TurboQuant startup tables. `tq_cb3` is the fitted 3-bit spherical codebook, `tq_cb3x4`
// expands packed 12-bit halves for the hot unpack path, `tq_rot`/`tq_rot_t` store the
// deterministic orthogonal rotation and its transpose, `tq_qjl` is the residual sketch
// matrix, and `tq_bits8` expands one sign byte into eight +/-1 floats for bit-dot queries.
static float tq_cb3[8];
static float tq_cb3x4[4096][4];
static float tq_rot[QK_TQ * QK_TQ];
static float tq_rot_t[QK_TQ * QK_TQ];
static float tq_qjl[QK_TQ * QK_TQ];
static float tq_bits8[256][8];

// Acceleration stuff
#if defined(__AVX2__)
#include "arch_avx2.h"
#endif
//#if defined(__aarch64__) && defined(__ARM_NEON) && defined(__ARM_FEATURE_DOTPROD)
//#include "arch_neon.h"
//#endif

static inline float fp32_from_bits(uint32_t w)
{
    float r;
    memcpy(&r,&w,4);
    return r;
}

static inline uint32_t fp32_to_bits(float f)
{
    uint32_t r;
    memcpy(&r,&f,4);
    return r;
}

static float fp16_to_fp32_impl(uint16_t h)
{
    const uint32_t w = (uint32_t) h << 16;
    const uint32_t sign = w & UINT32_C(0x80000000);
    const uint32_t two_w = w + w;

    const uint32_t exp_offset = UINT32_C(0xE0) << 23;
    const float exp_scale = fp32_from_bits(UINT32_C(0x7800000));

    const float normalized_value = fp32_from_bits((two_w >> 4) + exp_offset) * exp_scale;

    const uint32_t magic_mask = UINT32_C(126) << 23;
    const float magic_bias = 0.5f;
    const float denormalized_value = fp32_from_bits((two_w >> 17) | magic_mask) - magic_bias;

    const uint32_t denormalized_cutoff = UINT32_C(1) << 27;
    const uint32_t result = sign | (two_w < denormalized_cutoff ? fp32_to_bits(denormalized_value) : fp32_to_bits(normalized_value));
    return fp32_from_bits(result);
}

static inline uint16_t fp32_to_fp16(float f)
{
    const float scale_to_inf = fp32_from_bits(UINT32_C(0x77800000));
    const float scale_to_zero = fp32_from_bits(UINT32_C(0x08800000));

    float base = (fabsf(f) * scale_to_inf) * scale_to_zero;

    const uint32_t w = fp32_to_bits(f);
    const uint32_t shl1_w = w + w;
    const uint32_t sign = w & UINT32_C(0x80000000);
    uint32_t bias = shl1_w & UINT32_C(0xFF000000);
    if (bias < UINT32_C(0x71000000)) bias = UINT32_C(0x71000000);

    base = fp32_from_bits((bias >> 1) + UINT32_C(0x07800000)) + base;
    const uint32_t bits = fp32_to_bits(base);
    const uint32_t exp_bits = (bits >> 13) & UINT32_C(0x00007C00);
    const uint32_t mantissa_bits = bits & UINT32_C(0x00000FFF);
    const uint32_t nonsign = exp_bits + mantissa_bits;
    return (sign >> 16) | (shl1_w > UINT32_C(0xFF000000) ? UINT16_C(0x7E00) : nonsign);
}

void fp1632_init()
{
    for (int i = 0; i < 65536; i++) fp1632_lut[i] = fp16_to_fp32_impl((uint16_t)i);
}

void f32_to_f16_row(const float* x, gguf_half* y, int n)
{
    for (int i = 0; i < n; i++) y[i] = fp32_to_fp16(x[i]);
}

static inline void f16_to_f32_row(const gguf_half* x, float* y, int n)
{
    for (int i = 0; i < n; i++) y[i] = fp16_to_fp32(x[i]);
}

float inline dot_f32(const float* a, const float* b, int size);
float inline dot_f16_f32(const gguf_half* a, const float* b, int size);

void dequantize_row_q8_0(const block_q8_0* x, float* y, int64_t k)
{
    const int nb = k / QK8_0;

    for (int i = 0; i < nb; i++) {
        const float d = fp16_to_fp32(x[i].d);
        for (int j = 0; j < QK8_0; j++) y[i*QK8_0 + j] = x[i].qs[j] * d;
    }
}

void dequantize_row_q1_0(const block_q1_0* x, float* y, int64_t k)
{
    const int nb = k / QK8_0;

    for (int i = 0; i < nb; i++) {
        const float d = fp16_to_fp32(x[i].d);
        for (int j = 0; j < QK8_0; j++) {
            int b = (x[i].qs[j >> 3] >> (j & 7)) & 1;
            y[i*QK8_0 + j] = d * (b? 1.0f : -1.0f);
        }
    }
}

void dequantize_row_q1_G(const block_q1_G* x, float* y, int64_t k)
{
    const int nb = k / 128;

    for (int i = 0; i < nb; i++) {
        const float d = fp16_to_fp32(x[i].d);
        for (int j = 0; j < 128; j++) {
            int b = (x[i].qs[j >> 3] >> (j & 7)) & 1;
            y[i*128 + j] = d * (b? 1.0f : -1.0f);
        }
    }
}

static inline void get_scale_min_k4(int j, const uint8_t* q, uint8_t* d, uint8_t* m)
{
    if (j < 4) {
        *d = q[j] & 63;
        *m = q[j + 4] & 63;
    } else {
        *d = (q[j+4] & 0xF) | ((q[j-4] >> 6) << 4);
        *m = (q[j+4] >> 4) | ((q[j-0] >> 6) << 4);
    }
}

void dequantize_row_q2_K(const block_q2_K* x, float* y, int64_t k)
{
    const int nb = k / QK_K;

    for (int i = 0; i < nb; i++) {
        const float d = fp16_to_fp32(x[i].d);
        const float min = fp16_to_fp32(x[i].dmin);
        const uint8_t* q = x[i].qs;

        int is = 0;
        for (int n = 0; n < QK_K; n += 128) {
            int shift = 0;
            for (int j = 0; j < 4; j++) {
                uint8_t sc = x[i].scales[is++];
                float dl = d * (sc & 0xF), ml = min * (sc >> 4);
                for (int l = 0; l < 16; l++) *y++ = dl * ((int8_t)((q[l] >> shift) & 3)) - ml;

                sc = x[i].scales[is++];
                dl = d * (sc & 0xF), ml = min * (sc >> 4);
                for (int l = 0; l < 16; l++) *y++ = dl * ((int8_t)((q[l+16] >> shift) & 3)) - ml;

                shift += 2;
            }
            q += 32;
        }
    }
}

void dequantize_row_q3_K(const block_q3_K* x, float* y, int64_t k)
{
    // Q3_K stores 256 values as:
    // - 2-bit payload in qs[64]
    // - one extra sign/high-bit plane in hmask[32]
    // - 16 signed scales packed into scales[12]
    // - one fp16 block scale d
    const int nb = k / QK_K;
    const uint32_t kmask1 = 0x03030303;
    const uint32_t kmask2 = 0x0f0f0f0f;
    uint32_t aux[4];
    const int8_t* scales = (const int8_t*)aux;

    for (int i = 0; i < nb; i++) {
        const float d_all = fp16_to_fp32(x[i].d);
        const uint8_t* q = x[i].qs;
        const uint8_t* hm = x[i].hmask;
        uint8_t m = 1;

        memcpy(aux, x[i].scales, 12);
        uint32_t tmp = aux[2];
        aux[2] = ((aux[0] >> 4) & kmask2) | (((tmp >> 4) & kmask1) << 4);
        aux[3] = ((aux[1] >> 4) & kmask2) | (((tmp >> 6) & kmask1) << 4);
        aux[0] = (aux[0] & kmask2) | (((tmp >> 0) & kmask1) << 4);
        aux[1] = (aux[1] & kmask2) | (((tmp >> 2) & kmask1) << 4);

        int is = 0;
        for (int n = 0; n < QK_K; n += 128) {
            int shift = 0;
            for (int j = 0; j < 4; j++) {
                float dl = d_all * (scales[is++] - 32);
                for (int l = 0; l < 16; l++) *y++ = dl * ((int8_t)((q[l+ 0] >> shift) & 3) - ((hm[l+ 0] & m) ? 0 : 4));

                dl = d_all * (scales[is++] - 32);
                for (int l = 0; l < 16; l++) *y++ = dl * ((int8_t)((q[l+16] >> shift) & 3) - ((hm[l+16] & m) ? 0 : 4));

                shift += 2;
                m <<= 1;
            }
            q += 32;
        }
    }
}

void dequantize_row_q4_K(const block_q4_K* x, float* y, int64_t k)
{
    // Q4_K stores 256 values as 4-bit payloads in qs[128], plus 8 scale/min pairs
    // packed into scales[12]. d scales the values, dmin scales the per-subblock offsets.
    const int nb = k / QK_K;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q = x[i].qs;
        const float d = fp16_to_fp32(x[i].d);
        const float min = fp16_to_fp32(x[i].dmin);

        int is = 0;
        uint8_t sc, m;
        for (int j = 0; j < QK_K; j += 64) {
            get_scale_min_k4(is + 0, x[i].scales, &sc, &m);
            const float d1 = d * sc, m1 = min * m;
            get_scale_min_k4(is + 1, x[i].scales, &sc, &m);
            const float d2 = d * sc, m2 = min * m;
            for (int l = 0; l < 32; l++) *y++ = d1 * (q[l] & 0xF) - m1;
            for (int l = 0; l < 32; l++) *y++ = d2 * (q[l] >> 4) - m2;
            q += 32;
            is += 2;
        }
    }
}

void dequantize_row_q5_K(const block_q5_K* x, float* y, int64_t k)
{
    // Q5_K is Q4_K plus one extra bit plane in qh[32], so each value is 5 bits
    // with the same packed scale/min layout and shared block scales d/dmin.
    const int64_t nb = k / QK_K;

    for (int i = 0; i < nb; i++) {
        const uint8_t* ql = x[i].qs;
        const uint8_t* qh = x[i].qh;
        const float d = fp16_to_fp32(x[i].d);
        const float min = fp16_to_fp32(x[i].dmin);

        int is = 0;
        uint8_t sc, m;
        uint8_t u1 = 1, u2 = 2;
        for (int j = 0; j < QK_K; j += 64) {
            get_scale_min_k4(is + 0, x[i].scales, &sc, &m);
            const float d1 = d * sc, m1 = min * m;
            get_scale_min_k4(is + 1, x[i].scales, &sc, &m);
            const float d2 = d * sc, m2 = min * m;
            for (int l = 0; l < 32; l++) *y++ = d1 * ((ql[l] & 0xF) + (qh[l] & u1 ? 16 : 0)) - m1;
            for (int l = 0; l < 32; l++) *y++ = d2 * ((ql[l] >> 4) + (qh[l] & u2 ? 16 : 0)) - m2;
            ql += 32;
            is += 2;
            u1 <<= 2;
            u2 <<= 2;
        }
    }
}

void dequantize_row_q6_K(const block_q6_K* x, float* y, int64_t k)
{
    // Q6_K stores low 4 bits in ql[128], high 2 bits in qh[64], one signed scale
    // per 16 values in scales[16], and a shared fp16 block scale d.
    const int64_t nb = k / QK_K;

    for (int i = 0; i < nb; i++) {
        const float d = fp16_to_fp32(x[i].d);
        const uint8_t* ql = x[i].ql;
        const uint8_t* qh = x[i].qh;
        const int8_t* sc = x[i].scales;

        for (int n = 0; n < QK_K; n += 128) {
            for (int l = 0; l < 32; l++) {
                int is = l / 16;
                const int8_t q1 = (int8_t)((ql[l +  0] & 0xF) | (((qh[l] >> 0) & 3) << 4)) - 32;
                const int8_t q2 = (int8_t)((ql[l + 32] & 0xF) | (((qh[l] >> 2) & 3) << 4)) - 32;
                const int8_t q3 = (int8_t)((ql[l +  0] >> 4) | (((qh[l] >> 4) & 3) << 4)) - 32;
                const int8_t q4 = (int8_t)((ql[l + 32] >> 4) | (((qh[l] >> 6) & 3) << 4)) - 32;
                y[l +  0] = d * sc[is + 0] * q1;
                y[l + 32] = d * sc[is + 2] * q2;
                y[l + 64] = d * sc[is + 4] * q3;
                y[l + 96] = d * sc[is + 6] * q4;
            }
            y += 128;
            ql += 64;
            qh += 32;
            sc += 8;
        }
    }
}

void dequantize_row(gguf_type t, const void* in, float* out, int64_t n)
{
    switch (t) {
    case F32: memcpy(out,in,n*sizeof(float)); break;
    case F16: f16_to_f32_row((const gguf_half*)in,out,n); break;
    case Q8_0: dequantize_row_q8_0((block_q8_0*)in,out,n); break;
    case Q1_0: dequantize_row_q1_0((block_q1_0*)in,out,n); break;
    case Q1_G: dequantize_row_q1_G((block_q1_G*)in,out,n); break;
    case Q2_K: dequantize_row_q2_K((block_q2_K*)in,out,n); break;
    case Q3_K: dequantize_row_q3_K((block_q3_K*)in,out,n); break;
    case Q4_K: dequantize_row_q4_K((block_q4_K*)in,out,n); break;
    case Q5_K: dequantize_row_q5_K((block_q5_K*)in,out,n); break;
    case Q6_K: dequantize_row_q6_K((block_q6_K*)in,out,n); break;
    default:
        ERR("Unsupported tensor type %d",t);
        abort();
    }
}

float vec_dot_f32(const float* a, const float* b, int size)
{
    return dot_f32(a,b,size);
}

float vec_dot_f16_f32(const gguf_half* a, const float* b, int size)
{
    return dot_f16_f32(a,b,size);
}

void quantize_q8_0(qtensor &y, const ftensor &x)
{
    const int nb = x.size() / QK8_0;
    if ((int)y.size() < nb) y.resize(nb);

    MULTITHREAD
    for (int i = 0; i < nb; i++) {
        float amax = 0.0f; // absolute max

        for (int j = 0; j < QK8_0; j++) {
            const float v = x[i*QK8_0 + j];
            amax = max(amax, fabsf(v));
        }

        const float d = amax / ((1 << 7) - 1);
        const float id = d ? 1.0f/d : 0.0f;

        y[i].d = fp32_to_fp16(d);

        for (int j = 0; j < QK8_0; j++)
            y[i].qs[j] = roundf(x[i * QK8_0 + j] * id);
    }
}

static inline int nearest_int(float f)
{
    return f < 0 ? (int)(f - 0.5f) : (int)(f + 0.5f);
}

void quantize_q8_K(ktensor &y, const ftensor &x)
{
    assert(x.size() % QK_K == 0);
    const int nb = x.size() / QK_K;

    MULTITHREAD
    for (int i = 0; i < nb; i++) {
        const float* p = x.data() + i * QK_K;
        float max = 0.0f;
        float amax = 0.0f;
        for (int j = 0; j < QK_K; j++) {
            float ax = fabsf(p[j]);
            if (ax > amax) {
                amax = ax;
                max = p[j];
            }
        }
        if (!amax) {
            y[i].d = 0.0f;
            memset(y[i].qs,0,QK_K);
            memset(y[i].bsums,0,sizeof(y[i].bsums));
            continue;
        }

        const float iscale = -127.0f / max;
        for (int j = 0; j < QK_K; j++) {
            int v = nearest_int(iscale * p[j]);
            y[i].qs[j] = min(127,v);
        }
        for (int j = 0; j < QK_K/16; j++) {
            int sum = 0;
            for (int ii = 0; ii < 16; ii++) sum += y[i].qs[j*16 + ii];
            y[i].bsums[j] = sum;
        }
        y[i].d = 1.0f / iscale;
    }
}

#ifndef ARCH_ACCEL
float inline dot_f32(const float* a, const float* b, int size)
{
    float sum = 0.0f;
    for (int i = 0; i < size; i++) sum += a[i] * b[i];
    return sum;
}

float inline dot_f16_f32(const gguf_half* a, const float* b, int size)
{
    float sum = 0.0f;
    for (int i = 0; i < size; i++) sum += fp16_to_fp32(a[i]) * b[i];
    return sum;
}

float inline dot_q8_0_q8_0(const block_q8_0* x, const block_q8_0* y, int n)
{
    const int nb = n / QK8_0;
    float acc = 0.0f;
    for (int s,j,i = 0; i < nb; i++) {
        s = 0;
        for (j = 0; j < QK8_0; j++) s += x[i].qs[j] * y[i].qs[j];
        acc += fp16_to_fp32(x[i].d) * fp16_to_fp32(y[i].d) * s;
    }
    return acc;
}

float inline dot_q8_0_q1_0(const block_q8_0* y, const block_q1_0* x, int n)
{
    const int nb = n / QK8_0;
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        int sumi = 0;
        for (int j = 0; j < QK8_0; j++) {
            const int xi = ((x[i].qs[j >> 3] >> (j & 7)) & 1)? 1 : -1;
            sumi += xi * y[i].qs[j];
        }
        sumf += fp16_to_fp32(x[i].d) * fp16_to_fp32(y[i].d) * sumi;
    }

    return sumf;
}

float inline dot_q8_0_q1_G(const block_q8_0* y, const block_q1_G* x, int n)
{
    const int nb = n / 128;
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        const float d0 = fp16_to_fp32(x[i].d);
        float sumi = 0.0f;

        for (int k = 0; k < 4; k++) {
            const block_q8_0* yb = y + i * 4 + k;
            int sumib = 0;
            for (int j = 0; j < QK8_0; j++) {
                const int bit = k * QK8_0 + j;
                const int xi = ((x[i].qs[bit >> 3] >> (bit & 7)) & 1)? 1 : -1;
                sumib += xi * yb->qs[j];
            }
            sumi += fp16_to_fp32(yb->d) * sumib;
        }

        sumf += d0 * sumi;
    }

    return sumf;
}

float inline dot_q8_K_q3_K(const block_q8_K* y, const block_q3_K* x, int n)
{
    // Rebuild the signed Q3 values into aux8[], expand the packed per-16 scales,
    // then accumulate the dot in 16-value chunks against the runtime Q8_K activation.
    const int nb = n / QK_K;
    const uint32_t kmask1 = 0x03030303;
    const uint32_t kmask2 = 0x0f0f0f0f;
    int8_t aux8[QK_K];
    int16_t aux16[8];
    float sums[8];
    int32_t aux32[8];
    memset(sums,0,sizeof(sums));
    uint32_t auxs[4];
    const int8_t* scales = (const int8_t*)auxs;
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q3 = x[i].qs;
        const uint8_t* hm = x[i].hmask;
        const int8_t* q8 = y[i].qs;
        memset(aux32,0,sizeof(aux32));
        int8_t* a = aux8;
        uint8_t m = 1;
        // Expand the 2-bit payload plus high-bit mask into signed 3-bit values.
        for (int j = 0; j < QK_K; j += 128) {
            for (int l = 0; l < 32; l++) a[l] = q3[l] & 3;
            for (int l = 0; l < 32; l++) a[l] -= (hm[l] & m ? 0 : 4);
            a += 32;
            m <<= 1;
            for (int l = 0; l < 32; l++) a[l] = (q3[l] >> 2) & 3;
            for (int l = 0; l < 32; l++) a[l] -= (hm[l] & m ? 0 : 4);
            a += 32;
            m <<= 1;
            for (int l = 0; l < 32; l++) a[l] = (q3[l] >> 4) & 3;
            for (int l = 0; l < 32; l++) a[l] -= (hm[l] & m ? 0 : 4);
            a += 32;
            m <<= 1;
            for (int l = 0; l < 32; l++) a[l] = (q3[l] >> 6) & 3;
            for (int l = 0; l < 32; l++) a[l] -= (hm[l] & m ? 0 : 4);
            a += 32;
            m <<= 1;
            q3 += 32;
        }

        a = aux8;
        memcpy(auxs,x[i].scales,12);

        // Expand the packed sub-block scales to one signed scale per 16 values.
        uint32_t tmp = auxs[2];
        auxs[2] = ((auxs[0] >> 4) & kmask2) | (((tmp >> 4) & kmask1) << 4);
        auxs[3] = ((auxs[1] >> 4) & kmask2) | (((tmp >> 6) & kmask1) << 4);
        auxs[0] = (auxs[0] & kmask2) | (((tmp >> 0) & kmask1) << 4);
        auxs[1] = (auxs[1] & kmask2) | (((tmp >> 2) & kmask1) << 4);

        for (int j = 0; j < QK_K/16; j++) {
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += (scales[j] - 32) * aux16[l];
            q8 += 8;
            a += 8;
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += (scales[j] - 32) * aux16[l];
            q8 += 8;
            a += 8;
        }

        const float d = fp16_to_fp32(x[i].d) * y[i].d;
        for (int l = 0; l < 8; l++) sums[l] += d * aux32[l];
    }

    for (int l = 0; l < 8; l++) sumf += sums[l];
    return sumf;
}

float inline dot_q8_K_q2_K(const block_q8_K* y, const block_q2_K* x, int n)
{
    const int nb = n / QK_K;
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q2 = x[i].qs;
        const int8_t* q8 = y[i].qs;
        const uint8_t* sc = x[i].scales;

        int summs = 0;
        for (int j = 0; j < QK_K/16; j++) summs += y[i].bsums[j] * (sc[j] >> 4);

        const float dall = y[i].d * fp16_to_fp32(x[i].d);
        const float dmin = y[i].d * fp16_to_fp32(x[i].dmin);

        int isum = 0;
        int is = 0;
        for (int k = 0; k < QK_K/128; k++) {
            int shift = 0;
            for (int j = 0; j < 4; j++) {
                int d = sc[is++] & 0xF;
                int isuml = 0;
                for (int l = 0; l < 16; l++) isuml += q8[l] * ((q2[l] >> shift) & 3);
                isum += d * isuml;

                d = sc[is++] & 0xF;
                isuml = 0;
                for (int l = 16; l < 32; l++) isuml += q8[l] * ((q2[l] >> shift) & 3);
                isum += d * isuml;

                shift += 2;
                q8 += 32;
            }
            q2 += 32;
        }

        sumf += dall * isum - dmin * summs;
    }

    return sumf;
}

float inline dot_q8_K_q4_K(const block_q8_K* y, const block_q4_K* x, int n)
{
    // Walk the two 32-value halves inside each 64-value chunk, dot the activation
    // against each nibble stream, then apply the chunk scale and min correction.
    const int nb = n / QK_K;
    float acc = 0.0f;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q = x[i].qs;
        const int8_t* a = y[i].qs;
        const float d = fp16_to_fp32(x[i].d) * y[i].d;
        const float dmin = fp16_to_fp32(x[i].dmin) * y[i].d;
        int is = 0;

        // Each 64-value chunk is two 32-value dots with separate scale/min pairs.
        for (int j = 0; j < QK_K; j += 64) {
            uint8_t sc, m;
            get_scale_min_k4(is + 0,x[i].scales,&sc,&m);
            int dot1 = 0;
            for (int l = 0; l < 32; l++) dot1 += a[l] * (q[l] & 0xF);
            acc += d * sc * dot1 - dmin * m * (y[i].bsums[is*2 + 0] + y[i].bsums[is*2 + 1]);

            get_scale_min_k4(is + 1,x[i].scales,&sc,&m);
            int dot2 = 0;
            for (int l = 0; l < 32; l++) dot2 += a[l + 32] * (q[l] >> 4);
            acc += d * sc * dot2 - dmin * m * (y[i].bsums[is*2 + 2] + y[i].bsums[is*2 + 3]);

            q += 32;
            a += 64;
            is += 2;
        }
    }

    return acc;
}

float inline dot_q8_K_q5_K(const block_q8_K* y, const block_q5_K* x, int n)
{
    // Rebuild the 5-bit payload into aux8[], expand the packed scale/min tables,
    // accumulate scaled dot products, then subtract the min contribution via bsums.
    const int nb = n / QK_K;
    static const uint32_t kmask1 = 0x3f3f3f3f;
    static const uint32_t kmask2 = 0x0f0f0f0f;
    static const uint32_t kmask3 = 0x03030303;
    uint32_t utmp[4];
    const uint8_t* scales = (const uint8_t*)&utmp[0];
    const uint8_t* mins = (const uint8_t*)&utmp[2];
    int8_t aux8[QK_K];
    int16_t aux16[8];
    float sums[8];
    int32_t aux32[8];
    memset(sums,0,sizeof(sums));
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q4 = x[i].qs;
        const uint8_t* hm = x[i].qh;
        const int8_t* q8 = y[i].qs;
        memset(aux32,0,sizeof(aux32));
        int8_t* a = aux8;
        uint8_t m = 1;

        // Rebuild the 5-bit values before applying scale and min corrections.
        for (int j = 0; j < QK_K/64; j++) {
            for (int l = 0; l < 32; l++) a[l] = (int8_t)(q4[l] & 0xF);
            for (int l = 0; l < 32; l++) a[l] += (hm[l] & m ? 16 : 0);
            a += 32;
            m <<= 1;
            for (int l = 0; l < 32; l++) a[l] = (int8_t)(q4[l] >> 4);
            for (int l = 0; l < 32; l++) a[l] += (hm[l] & m ? 16 : 0);
            a += 32;
            m <<= 1;
            q4 += 32;
        }

        memcpy(utmp,x[i].scales,12);
        utmp[3] = ((utmp[2] >> 4) & kmask2) | (((utmp[1] >> 6) & kmask3) << 4);
        const uint32_t uaux = utmp[1] & kmask1;
        utmp[1] = (utmp[2] & kmask2) | (((utmp[0] >> 6) & kmask3) << 4);
        utmp[2] = uaux;
        utmp[0] &= kmask1;

        int sumi = 0;
        for (int j = 0; j < QK_K/16; j++) sumi += y[i].bsums[j] * mins[j/2];

        a = aux8;
        int is = 0;
        for (int j = 0; j < QK_K/32; j++) {
            int32_t scale = scales[is++];
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
        }

        const float d = fp16_to_fp32(x[i].d) * y[i].d;
        for (int l = 0; l < 8; l++) sums[l] += d * aux32[l];
        sumf -= fp16_to_fp32(x[i].dmin) * y[i].d * sumi;
    }

    for (int l = 0; l < 8; l++) sumf += sums[l];
    return sumf;
}

float inline dot_q8_K_q6_K(const block_q8_K* y, const block_q6_K* x, int n)
{
    const int nb = n / QK_K;
    int8_t aux8[QK_K];
    int16_t aux16[8];
    float sums[8];
    int32_t aux32[8];
    memset(sums,0,sizeof(sums));
    float sumf = 0.0f;

    for (int i = 0; i < nb; i++) {
        const uint8_t* q4 = x[i].ql;
        const uint8_t* qh = x[i].qh;
        const int8_t* q8 = y[i].qs;
        memset(aux32,0,sizeof(aux32));
        int8_t* a = aux8;
        for (int j = 0; j < QK_K; j += 128) {
            for (int l = 0; l < 32; l++) {
                a[l + 0] = (int8_t)((q4[l + 0] & 0xF) | (((qh[l] >> 0) & 3) << 4)) - 32;
                a[l + 32] = (int8_t)((q4[l + 32] & 0xF) | (((qh[l] >> 2) & 3) << 4)) - 32;
                a[l + 64] = (int8_t)((q4[l + 0] >> 4) | (((qh[l] >> 4) & 3) << 4)) - 32;
                a[l + 96] = (int8_t)((q4[l + 32] >> 4) | (((qh[l] >> 6) & 3) << 4)) - 32;
            }
            a += 128;
            q4 += 64;
            qh += 32;
        }

        a = aux8;
        int is = 0;
        for (int j = 0; j < QK_K/16; j++) {
            int scale = x[i].scales[is++];
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
            for (int l = 0; l < 8; l++) aux16[l] = q8[l] * a[l];
            for (int l = 0; l < 8; l++) aux32[l] += scale * aux16[l];
            q8 += 8; a += 8;
        }

        const float d = fp16_to_fp32(x[i].d) * y[i].d;
        for (int l = 0; l < 8; l++) sums[l] += d * aux32[l];
    }

    for (int l = 0; l < 8; l++) sumf += sums[l];
    return sumf;
}

float inline dot_tq_bits_f32(const uint8_t* bits, const float* x, int size)
{
    const int nfull = size >> 3;

    float sum = 0.0f;
    for (int bi = 0; bi < nfull; bi++) {
        const float* s = tq_bits8[bits[bi]];
        const float* p = x + bi * 8;
        sum += s[0] * p[0] + s[1] * p[1] + s[2] * p[2] + s[3] * p[3] + s[4] * p[4] + s[5] * p[5] + s[6] * p[6] + s[7] * p[7];
    }

    const int i0 = nfull << 3;
    for (int i = i0; i < size; i++) sum += (((bits[nfull] >> (i - i0)) & 1)? 1.0f : -1.0f) * x[i];
    return sum;
}

static inline void tq_madd_pack8(float* dst, float s, uint32_t pack)
{
    const float* c0 = tq_cb3x4[pack & 0xFFF];
    const float* c1 = tq_cb3x4[(pack >> 12) & 0xFFF];
    dst[0] += s * c0[0];
    dst[1] += s * c0[1];
    dst[2] += s * c0[2];
    dst[3] += s * c0[3];
    dst[4] += s * c1[0];
    dst[5] += s * c1[1];
    dst[6] += s * c1[2];
    dst[7] += s * c1[3];
}
#endif /*ARCH_ACCEL*/

static inline uint32_t tq_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= TQ_HASH_MUL0;
    x ^= x >> 15;
    x *= TQ_HASH_MUL1;
    x ^= x >> 16;
    return x;
}

static inline float tq_sign(uint32_t seed, int i)
{
    return (tq_hash32(seed ^ (uint32_t)i * TQ_HASH_GOLDEN) & 1)? -1.0f : 1.0f;
}

static inline bool tq_is_pow2(int n)
{
    return n > 0 && !(n & (n - 1));
}

static void tq_hadamard(float* x, int n)
{
    for (int step = 1; step < n; step <<= 1) {
        for (int i = 0; i < n; i += step << 1) {
            for (int j = 0; j < step; j++) {
                const float a = x[i + j];
                const float b = x[i + j + step];
                x[i + j] = a + b;
                x[i + j + step] = a - b;
            }
        }
    }

    const float s = 1.0f / sqrtf(n);
    for (int i = 0; i < n; i++) x[i] *= s;
}

static float tq_gauss(uint32_t seed, int i)
{
    // Build two deterministic uniforms from the hash stream, then turn them into a
    // Gaussian draw with Box-Muller so all startup tables are reproducible.
    float u1 = ((tq_hash32(seed ^ (uint32_t)(i * 2 + 0) * TQ_HASH_GOLDEN) >> 8) + 1.0f) / TQ_HASH_U24_DENOM_OPEN;
    float u2 = ((tq_hash32(seed ^ (uint32_t)(i * 2 + 1) * TQ_HASH_GOLDEN) >> 8) + 0.5f) / TQ_HASH_U24_DENOM;
    return sqrtf(-2.0f * logf(u1)) * cosf(TQ_TAU * u2);
}

static double tq_cb_interval_mean(const double* xg, const double* wg, double a, double b)
{
    double z = 0.0;
    double m = 0.0;
    bool first = true;
    double px = 0.0;
    double pw = 0.0;

    for (int i = 0; i < TQ_CB_GRID_SIZE; i++) {
        const double x = xg[i];
        if (x < a || x > b) continue;
        if (!first) {
            const double dx = x - px;
            z += 0.5 * (pw + wg[i]) * dx;
            m += 0.5 * (pw * px + wg[i] * x) * dx;
        }
        first = false;
        px = x;
        pw = wg[i];
    }

    return m / z;
}

static void tq_rotate_block(const float* x, float* y, int n)
{
    if (n == QK_TQ) {
        for (int i = 0; i < n; i++) {
            float sum = 0.0f;
            const float* r = tq_rot + i * QK_TQ;
            for (int j = 0; j < n; j++) sum += r[j] * x[j];
            y[i] = sum;
        }
        return;
    }
    for (int i = 0; i < n; i++) y[i] = x[i] * tq_sign(TQ_ROT_SEED,i);
    if (!tq_is_pow2(n)) return;
    tq_hadamard(y,n);
    for (int i = 0; i < n; i++) y[i] *= tq_sign(TQ_ROT_SEED ^ TQ_ROT_STAGE2,i);
    tq_hadamard(y,n);
}

static void tq_restore_block(const float* x, float* y, int n)
{
    if (n == QK_TQ) {
        for (int i = 0; i < n; i++) {
            float sum = 0.0f;
            const float* r = tq_rot_t + i * QK_TQ;
            for (int j = 0; j < n; j++) sum += r[j] * x[j];
            y[i] = sum;
        }
        return;
    }
    memcpy(y,x,n * sizeof(float));
    if (!tq_is_pow2(n)) {
        for (int i = 0; i < n; i++) y[i] *= tq_sign(TQ_ROT_SEED,i);
        return;
    }
    tq_hadamard(y,n);
    for (int i = 0; i < n; i++) y[i] *= tq_sign(TQ_ROT_SEED ^ TQ_ROT_STAGE2,i);
    tq_hadamard(y,n);
    for (int i = 0; i < n; i++) y[i] *= tq_sign(TQ_ROT_SEED,i);
}

static void tq_qjl_block(const float* x, float* y, int n)
{
    if (n == QK_TQ) {
        for (int i = 0; i < n; i++) {
            const float* r = tq_qjl + i * QK_TQ;
            float sum = 0.0f;
            for (int j = 0; j < n; j++) sum += r[j] * x[j];
            y[i] = sum;
        }
        return;
    }
    for (int i = 0; i < n; i++) y[i] = x[i] * tq_sign(TQ_QJL_SEED,i);
    if (tq_is_pow2(n)) tq_hadamard(y,n);
}

static inline int tq_pick_level(const float* cb, int nc, float v)
{
    int best = 0;
    float bestd = fabsf(v - cb[0]);
    for (int i = 1; i < nc; i++) {
        const float d = fabsf(v - cb[i]);
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

void tq_rotate(float* dst, const float* src, int n)
{
    const int nb = (n + QK_TQ - 1) / QK_TQ;
    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        tq_rotate_block(src + i * QK_TQ,dst + i * QK_TQ,nr);
    }
}

void quantize_tq_key_row(const float* x, block_tq_k* y, int n)
{
    const int nb = (n + QK_TQ - 1) / QK_TQ;
    float u[QK_TQ];
    float q[QK_TQ];
    float z[QK_TQ];
    float zh[QK_TQ];
    float r[QK_TQ];
    float rq[QK_TQ];

    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        const float* p = x + i * QK_TQ;
        memset(&y[i],0,sizeof(y[i]));

        float norm = 0.0f;
        for (int j = 0; j < nr; j++) norm += p[j] * p[j];
        norm = sqrtf(norm);
        y[i].d = fp32_to_fp16(norm);
        if (!norm) continue;

        for (int j = 0; j < nr; j++) u[j] = p[j] / norm;
        tq_rotate_block(u,q,nr);

        const float s = sqrtf(nr);
        for (int j = 0; j < nr; j += 8) {
            uint32_t pack = 0;
            for (int jj = 0; jj < 8 && j + jj < nr; jj++) {
                const int v = tq_pick_level(tq_cb3,8,q[j + jj] * s);
                pack |= (uint32_t)v << (jj * 3);
                z[j + jj] = tq_cb3[v] / s;
            }
            const int qp = j / 8 * 3;
            y[i].qs[qp + 0] = pack & 0xFF;
            y[i].qs[qp + 1] = (pack >> 8) & 0xFF;
            y[i].qs[qp + 2] = (pack >> 16) & 0xFF;
        }

        tq_restore_block(z,zh,nr);
        float rnorm = 0.0f;
        for (int j = 0; j < nr; j++) {
            r[j] = u[j] - zh[j];
            rnorm += r[j] * r[j];
        }
        rnorm = sqrtf(rnorm);
        y[i].rd = fp32_to_fp16(rnorm);
        if (!rnorm) continue;

        for (int j = 0; j < nr; j++) r[j] /= rnorm;
        tq_qjl_block(r,rq,nr);
        for (int j = 0; j < nr; j++) if (rq[j] >= 0.0f) y[i].signs[j >> 3] |= 1u << (j & 7);
    }
}

void quantize_tq_val_row(const float* x, block_tq_v* y, int n)
{
    const int nb = (n + QK_TQ - 1) / QK_TQ;
    float u[QK_TQ];
    float q[QK_TQ];

    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        const float* p = x + i * QK_TQ;
        memset(&y[i],0,sizeof(y[i]));

        float norm = 0.0f;
        for (int j = 0; j < nr; j++) norm += p[j] * p[j];
        norm = sqrtf(norm);
        y[i].d = fp32_to_fp16(norm);
        if (!norm) continue;

        for (int j = 0; j < nr; j++) u[j] = p[j] / norm;
        tq_rotate_block(u,q,nr);

        const float s = sqrtf(nr);
        for (int j = 0; j < nr; j += 8) {
            uint32_t pack = 0;
            for (int jj = 0; jj < 8 && j + jj < nr; jj++) {
                const int v = tq_pick_level(tq_cb3,8,q[j + jj] * s);
                pack |= (uint32_t)v << (jj * 3);
            }
            const int qp = j / 8 * 3;
            y[i].qs[qp + 0] = pack & 0xFF;
            y[i].qs[qp + 1] = (pack >> 8) & 0xFF;
            y[i].qs[qp + 2] = (pack >> 16) & 0xFF;
        }
    }
}

void prep_tq_query(const float* q, float* q0, float* q1, int n)
{
    const int nb = (n + QK_TQ - 1) / QK_TQ;
    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        tq_rotate_block(q + i * QK_TQ,q0 + i * QK_TQ,nr);
        tq_qjl_block(q + i * QK_TQ,q1 + i * QK_TQ,nr);
    }
}

void tq_restore(float* dst, const float* src, int n)
{
    const int nb = (n + QK_TQ - 1) / QK_TQ;
    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        tq_restore_block(src + i * QK_TQ,dst + i * QK_TQ,nr);
    }
}

float vec_dot_tq_key_query(const block_tq_k* k, const float* q0, const float* q1, int n)
{
    if (n == QK_TQ) {
        const float d = fp16_to_fp32(k[0].d);
        if (!d) return 0.0f;

        float sum = 0.0f;
        const float ds = d * TQ_BLOCK_INV_SQRT;
        const uint8_t* qs = k[0].qs;
        for (int j = 0, qp = 0; j < QK_TQ; j += 8, qp += 3) {
            const uint32_t pack = (uint32_t)qs[qp + 0] | ((uint32_t)qs[qp + 1] << 8) | ((uint32_t)qs[qp + 2] << 16);
            sum += ds * (
                tq_cb3[(pack >>  0) & 7] * q0[j + 0] +
                tq_cb3[(pack >>  3) & 7] * q0[j + 1] +
                tq_cb3[(pack >>  6) & 7] * q0[j + 2] +
                tq_cb3[(pack >>  9) & 7] * q0[j + 3] +
                tq_cb3[(pack >> 12) & 7] * q0[j + 4] +
                tq_cb3[(pack >> 15) & 7] * q0[j + 5] +
                tq_cb3[(pack >> 18) & 7] * q0[j + 6] +
                tq_cb3[(pack >> 21) & 7] * q0[j + 7]);
        }

        const float rd = fp16_to_fp32(k[0].rd);
        if (rd) sum += d * rd * TQ_QJL_BLOCK_SCALE * dot_tq_bits_f32(k[0].signs,q1,QK_TQ);
        return sum;
    }

    const int nb = (n + QK_TQ - 1) / QK_TQ;
    float sum = 0.0f;

    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        const float d = fp16_to_fp32(k[i].d);
        if (!d) continue;

        const float s = 1.0f / sqrtf(nr);
        const float* qrot = q0 + i * QK_TQ;
        if (nr == QK_TQ) {
            const float ds = d * s;
            const uint8_t* qs = k[i].qs;
            for (int j = 0, qp = 0; j < QK_TQ; j += 8, qp += 3) {
                const uint32_t pack = (uint32_t)qs[qp + 0] | ((uint32_t)qs[qp + 1] << 8) | ((uint32_t)qs[qp + 2] << 16);
                sum += ds * (
                    tq_cb3[(pack >>  0) & 7] * qrot[j + 0] +
                    tq_cb3[(pack >>  3) & 7] * qrot[j + 1] +
                    tq_cb3[(pack >>  6) & 7] * qrot[j + 2] +
                    tq_cb3[(pack >>  9) & 7] * qrot[j + 3] +
                    tq_cb3[(pack >> 12) & 7] * qrot[j + 4] +
                    tq_cb3[(pack >> 15) & 7] * qrot[j + 5] +
                    tq_cb3[(pack >> 18) & 7] * qrot[j + 6] +
                    tq_cb3[(pack >> 21) & 7] * qrot[j + 7]);
            }
        } else {
            for (int j = 0; j < nr; j += 8) {
                const int qp = j / 8 * 3;
                const uint32_t pack = (uint32_t)k[i].qs[qp + 0] | ((uint32_t)k[i].qs[qp + 1] << 8) | ((uint32_t)k[i].qs[qp + 2] << 16);
                for (int jj = 0; jj < 8 && j + jj < nr; jj++) {
                    const int q = (pack >> (jj * 3)) & 7;
                    sum += d * tq_cb3[q] * s * qrot[j + jj];
                }
            }
        }

        const float rd = fp16_to_fp32(k[i].rd);
        if (rd) sum += d * rd * (TQ_QJL_SCALE / nr) * dot_tq_bits_f32(k[i].signs,q1 + i * QK_TQ,nr);
    }

    return sum;
}

void vec_madd_tq_val(float* dst, const block_tq_v* v, float a, int n)
{
    if (n == QK_TQ) {
        const float d = a * fp16_to_fp32(v[0].d);
        if (!d) return;

        float* p = dst;
        const float s = d * TQ_BLOCK_INV_SQRT;
        const uint8_t* qs = v[0].qs;
        for (int j = 0, qp = 0; j < QK_TQ; j += 8, qp += 3) {
            const uint32_t pack = (uint32_t)qs[qp + 0] | ((uint32_t)qs[qp + 1] << 8) | ((uint32_t)qs[qp + 2] << 16);
            tq_madd_pack8(p + j,s,pack);
        }
        return;
    }

    const int nb = (n + QK_TQ - 1) / QK_TQ;

    for (int i = 0; i < nb; i++) {
        const int nr = MIN(QK_TQ,n - i * QK_TQ);
        float* p = dst + i * QK_TQ;
        const float d = a * fp16_to_fp32(v[i].d);
        if (!d) continue;

        const float s = d / sqrtf(nr);
        for (int j = 0; j < nr; j += 8) {
            const int qp = j / 8 * 3;
            const uint32_t pack = (uint32_t)v[i].qs[qp + 0] | ((uint32_t)v[i].qs[qp + 1] << 8) | ((uint32_t)v[i].qs[qp + 2] << 16);
            if (j + 8 <= nr) {
                tq_madd_pack8(p + j,s,pack);
                continue;
            }
            const float* c0 = tq_cb3x4[pack & 0xFFF];
            const float* c1 = tq_cb3x4[(pack >> 12) & 0xFFF];
            for (int jj = 0; jj < 4 && j + jj < nr; jj++) p[j + jj] += s * c0[jj];
            for (int jj = 0; jj < 4 && j + 4 + jj < nr; jj++) p[j + 4 + jj] += s * c1[jj];
        }
    }
}

static void tq_init_codebook(float* cb, int nc, int dim)
{
    double xg[TQ_CB_GRID_SIZE];
    double wg[TQ_CB_GRID_SIZE];
    const double smax = sqrt((double)dim);
    const double step = 2.0 * smax / (double)(TQ_CB_GRID_SIZE - 1);
    const double logc = lgamma(0.5 * dim) - 0.5 * log(M_PI * dim) - lgamma(0.5 * (dim - 1));
    double cur[16];
    double nxt[16];
    double bounds[17];

    assert(nc <= 16);
    for (int i = 0; i < TQ_CB_GRID_SIZE; i++) {
        xg[i] = -smax + step * i;
        const double t = 1.0 - xg[i] * xg[i] / dim;
        wg[i] = t > 0.0? exp(logc + 0.5 * (dim - 3) * log(t)) : 0.0;
    }
    for (int i = 0; i < nc; i++) cur[i] = -smax + (2.0 * i + 1.0) * smax / (double)nc;

    for (int it = 0; it < TQ_CB_MAX_ITERS; it++) {
        bounds[0] = -smax;
        bounds[nc] = smax;
        for (int i = 1; i < nc; i++) bounds[i] = 0.5 * (cur[i - 1] + cur[i]);
        for (int i = 0; i < nc; i++) nxt[i] = tq_cb_interval_mean(xg,wg,bounds[i],bounds[i + 1]);

        double md = 0.0;
        for (int i = 0; i < nc; i++) {
            md = MAX(md,fabs(nxt[i] - cur[i]));
            cur[i] = nxt[i];
        }
        if (md < TQ_CB_EPS) break;
    }

    for (int i = 0; i < nc; i++) cb[i] = cur[i];
}

void tq_init()
{
    tq_init_codebook(tq_cb3,8,QK_TQ);

    for (int i = 0; i < 4096; i++) {
        tq_cb3x4[i][0] = tq_cb3[(i >>  0) & 7];
        tq_cb3x4[i][1] = tq_cb3[(i >>  3) & 7];
        tq_cb3x4[i][2] = tq_cb3[(i >>  6) & 7];
        tq_cb3x4[i][3] = tq_cb3[(i >>  9) & 7];
    }

    for (int i = 0; i < QK_TQ * QK_TQ; i++) tq_qjl[i] = tq_gauss(TQ_QJL_SEED,i);

    for (int b = 0; b < 256; b++) {
        for (int i = 0; i < 8; i++)
            tq_bits8[b][i] = (b & (1 << i))? 1.0f : -1.0f;
    }

    for (int c = 0; c < QK_TQ; c++) {
        for (int r = 0; r < QK_TQ; r++) tq_rot[r * QK_TQ + c] = tq_gauss(TQ_ROT_SEED,c * QK_TQ + r);
        for (int p = 0; p < c; p++) {
            double dot = 0.0;
            for (int r = 0; r < QK_TQ; r++) dot += (double)tq_rot[r * QK_TQ + c] * tq_rot[r * QK_TQ + p];
            for (int r = 0; r < QK_TQ; r++) tq_rot[r * QK_TQ + c] -= (float)(dot * tq_rot[r * QK_TQ + p]);
        }
        double norm = 0.0;
        for (int r = 0; r < QK_TQ; r++) norm += (double)tq_rot[r * QK_TQ + c] * tq_rot[r * QK_TQ + c];
        norm = sqrt(norm);
        for (int r = 0; r < QK_TQ; r++) tq_rot[r * QK_TQ + c] /= norm;
    }

    for (int r = 0; r < QK_TQ; r++) {
        for (int c = 0; c < QK_TQ; c++)
            tq_rot_t[r * QK_TQ + c] = tq_rot[c * QK_TQ + r];
    }
}

// out: destination vector with d rows
// src: float activation to quantize on demand when the weight is Q8_0
// qxk/qx8: pre-quantized activation in Q8_K or Q8_0 form
// w: source weight tensor row layout
// n: input width, d: output width
void matmul(ftensor &out, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w, int n, int d)
{
    const block_q8_0* q8 = qx8 ? qx8->data() : NULL;
    if ((w.type == Q8_0 || w.type == Q1_0 || w.type == Q1_G) && !q8) {
        assert(src);
        quantize_q8_0(m_xq8,*src);
        q8 = m_xq8.data();
    }

    MULTITHREAD
    for (int r = 0; r < d; r++) {
        uint8_t* wp = w.ptr + r * w.rsz;
        switch (w.type) {
            case F32: out[r] = dot_f32((float*)wp,src->data(),n); break;
            case F16: out[r] = dot_f16_f32((gguf_half*)wp,src->data(),n); break;
            case Q8_0: out[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp,n); break;
            case Q1_0: out[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp,n); break;
            case Q1_G: out[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp,n); break;
            case Q2_K: out[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp,n); break;
            case Q3_K: out[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp,n); break;
            case Q4_K: out[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp,n); break;
            case Q5_K: out[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp,n); break;
            case Q6_K: out[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp,n); break;
            default:
                ERR("Unsupported matmul type %d",w.type);
                abort();
        }
    }
}

// Same contract as matmul(), but computes two same-shape outputs in one row loop.
void matmul2(ftensor &out0, ftensor &out1, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w0, wtensor w1, int n, int d)
{
    const block_q8_0* q8 = qx8 ? qx8->data() : NULL;
    if ((w0.type == Q8_0 || w0.type == Q1_0 || w0.type == Q1_G || w1.type == Q8_0 || w1.type == Q1_0 || w1.type == Q1_G) && !q8) {
        assert(src);
        quantize_q8_0(m_xq8,*src);
        q8 = m_xq8.data();
    }

    MULTITHREAD
    for (int r = 0; r < d; r++) {
        uint8_t* wp0 = w0.ptr + r * w0.rsz;
        uint8_t* wp1 = w1.ptr + r * w1.rsz;

        switch (w0.type) {
            case F32: out0[r] = dot_f32((float*)wp0,src->data(),n); break;
            case F16: out0[r] = dot_f16_f32((gguf_half*)wp0,src->data(),n); break;
            case Q8_0: out0[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp0,n); break;
            case Q1_0: out0[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp0,n); break;
            case Q1_G: out0[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp0,n); break;
            case Q2_K: out0[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp0,n); break;
            case Q3_K: out0[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp0,n); break;
            case Q4_K: out0[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp0,n); break;
            case Q5_K: out0[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp0,n); break;
            case Q6_K: out0[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp0,n); break;
            default:
                ERR("Unsupported matmul type %d",w0.type);
                abort();
        }

        switch (w1.type) {
            case F32: out1[r] = dot_f32((float*)wp1,src->data(),n); break;
            case F16: out1[r] = dot_f16_f32((gguf_half*)wp1,src->data(),n); break;
            case Q8_0: out1[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp1,n); break;
            case Q1_0: out1[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp1,n); break;
            case Q1_G: out1[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp1,n); break;
            case Q2_K: out1[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp1,n); break;
            case Q3_K: out1[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp1,n); break;
            case Q4_K: out1[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp1,n); break;
            case Q5_K: out1[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp1,n); break;
            case Q6_K: out1[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp1,n); break;
            default:
                ERR("Unsupported matmul type %d",w1.type);
                abort();
        }
    }
}

void matmul3(ftensor &out0, ftensor &out1, ftensor &out2, const ftensor* src, const ktensor* qxk, const qtensor* qx8, wtensor w0, wtensor w1, wtensor w2, int n, int d0, int d1, int d2)
{
    const block_q8_0* q8 = qx8 ? qx8->data() : NULL;
    if ((w0.type == Q8_0 || w0.type == Q1_0 || w0.type == Q1_G || w1.type == Q8_0 || w1.type == Q1_0 || w1.type == Q1_G || w2.type == Q8_0 || w2.type == Q1_0 || w2.type == Q1_G) && !q8) {
        assert(src);
        quantize_q8_0(m_xq8,*src);
        q8 = m_xq8.data();
    }

    int d = d0;
    if (d1 > d) d = d1;
    if (d2 > d) d = d2;

    MULTITHREAD
    for (int r = 0; r < d; r++) {
        if (r < d0) {
            uint8_t* wp0 = w0.ptr + r * w0.rsz;
            switch (w0.type) {
                case F32: out0[r] = dot_f32((float*)wp0,src->data(),n); break;
                case F16: out0[r] = dot_f16_f32((gguf_half*)wp0,src->data(),n); break;
                case Q8_0: out0[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp0,n); break;
                case Q1_0: out0[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp0,n); break;
                case Q1_G: out0[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp0,n); break;
                case Q2_K: out0[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp0,n); break;
                case Q3_K: out0[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp0,n); break;
                case Q4_K: out0[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp0,n); break;
                case Q5_K: out0[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp0,n); break;
                case Q6_K: out0[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp0,n); break;
                default:
                    ERR("Unsupported matmul type %d",w0.type);
                    abort();
            }
        }

        if (r < d1) {
            uint8_t* wp1 = w1.ptr + r * w1.rsz;
            switch (w1.type) {
                case F32: out1[r] = dot_f32((float*)wp1,src->data(),n); break;
                case F16: out1[r] = dot_f16_f32((gguf_half*)wp1,src->data(),n); break;
                case Q8_0: out1[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp1,n); break;
                case Q1_0: out1[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp1,n); break;
                case Q1_G: out1[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp1,n); break;
                case Q2_K: out1[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp1,n); break;
                case Q3_K: out1[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp1,n); break;
                case Q4_K: out1[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp1,n); break;
                case Q5_K: out1[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp1,n); break;
                case Q6_K: out1[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp1,n); break;
                default:
                    ERR("Unsupported matmul type %d",w1.type);
                    abort();
            }
        }

        if (r < d2) {
            uint8_t* wp2 = w2.ptr + r * w2.rsz;
            switch (w2.type) {
                case F32: out2[r] = dot_f32((float*)wp2,src->data(),n); break;
                case F16: out2[r] = dot_f16_f32((gguf_half*)wp2,src->data(),n); break;
                case Q8_0: out2[r] = dot_q8_0_q8_0(q8,(block_q8_0*)wp2,n); break;
                case Q1_0: out2[r] = dot_q8_0_q1_0(q8,(block_q1_0*)wp2,n); break;
                case Q1_G: out2[r] = dot_q8_0_q1_G(q8,(block_q1_G*)wp2,n); break;
                case Q2_K: out2[r] = dot_q8_K_q2_K(qxk->data(),(block_q2_K*)wp2,n); break;
                case Q3_K: out2[r] = dot_q8_K_q3_K(qxk->data(),(block_q3_K*)wp2,n); break;
                case Q4_K: out2[r] = dot_q8_K_q4_K(qxk->data(),(block_q4_K*)wp2,n); break;
                case Q5_K: out2[r] = dot_q8_K_q5_K(qxk->data(),(block_q5_K*)wp2,n); break;
                case Q6_K: out2[r] = dot_q8_K_q6_K(qxk->data(),(block_q6_K*)wp2,n); break;
                default:
                    ERR("Unsupported matmul type %d",w2.type);
                    abort();
            }
        }
    }
}

void rope(ftensor& x, int n_heads, int pos, const gguf_model &mdl, const ftensor &rope_freq)
{
    assert((int)x.size() == n_heads * mdl.head_dim);
    int rope_dim = mdl.rope_dim;
    if (rope_dim > mdl.head_dim) rope_dim = mdl.head_dim;
    if (rope_dim & 1) rope_dim--;
    const bool use_neox = mdl.arch == "qwen3";
    const int rope_half = rope_dim >> 1;

    for (int h = 0; h < n_heads; h++) {
        float* v = x.data() + h * mdl.head_dim;
        for (int i = 0; i < rope_dim; i += 2) {
            const int m = i >> 1;
            const float ang = pos * rope_freq[m];
            const float c = cosf(ang), s = sinf(ang);
            const int i0 = use_neox? m : i;
            const int i1 = use_neox? m + rope_half : i + 1;
            const float x0 = v[i0];
            const float x1 = v[i1];
            v[i0] = x0 * c - x1 * s;
            v[i1] = x0 * s + x1 * c;
        }
    }
}

void rmsnorm(ftensor &out, const ftensor &x, float* w, int size, float epsilon)
{
    float ss = 0.0f;
    for (int i = 0; i < size; i++) ss += x[i] * x[i];
    ss = ss / (float)size + epsilon;
    ss = 1.0f / sqrtf(ss);
    for (int i = 0; i < size; i++) out[i] = x[i] * ss * w[i];
}

void rmsnorm_inplace(float* x, float* w, int size, float epsilon)
{
    float ss = 0.0f;
    for (int i = 0; i < size; i++) ss += x[i] * x[i];
    ss = 1.0f / sqrtf(ss / (float)size + epsilon);
    for (int i = 0; i < size; i++) x[i] = x[i] * ss * w[i];
}

void l2norm(ftensor &x)
{
    float ss = 0.0f;
    for (int i = 0; i < (int)x.size(); i++) ss += x[i] * x[i];
    if (ss <= 0.0f) return;
    ss = 1.0f / sqrtf(ss);
    for (int i = 0; i < (int)x.size(); i++) x[i] *= ss;
}

void layernorm(ftensor &out, const ftensor &x, const float* w, const float* b, int size, float epsilon)
{
    double mean = 0.0;
    double var = 0.0;
    for (int i = 0; i < size; i++) mean += x[i];
    mean /= size;
    for (int i = 0; i < size; i++) {
        const double d = x[i] - mean;
        var += d * d;
    }
    var /= size;

    const float scale = 1.0f / sqrtf(var + epsilon);
    for (int i = 0; i < size; i++) out[i] = (x[i] - mean) * scale * w[i] + b[i];
}

void softmax(float* x, int size)
{
    float max_val = x[0];
    for (int i = 1; i < size; i++) if (x[i] > max_val) max_val = x[i];

    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }

    for (int i = 0; i < size; i++) x[i] /= sum;
}

void gelu(ftensor &x)
{
    for (int i = 0; i < (int)x.size(); i++) {
        const float v = x[i];
        x[i] = 0.5f * v * (1.0f + tanhf(GELU_TANH_SCALE * (v + GELU_TANH_CUBIC * v * v * v)));
    }
}
