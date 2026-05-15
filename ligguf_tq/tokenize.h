/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#pragma once

#include <vector>
#include <string>
#include "common.h"

bool is_byte_token(const char* s);
std::string tok_to_str(model_state* S, int tok);
std::vector<int> tokenize(model_state* S, const char* str, bool bos, bool eos);
