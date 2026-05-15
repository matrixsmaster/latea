/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#pragma once

#include "common.h"
#include "lil_math.h"
#include "tokenize.h"

void inference(model_state* S, int tok);
int sampler_greedy(model_state* S);
int sampler_topp(model_state* S);
