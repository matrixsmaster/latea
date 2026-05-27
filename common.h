#pragma once

#include <string>
#include <stdint.h>

#define TITLE "Latea"
#define VERSION "0.3.7"
#define COPYRIGHT "(c) Dmitry 'sciloaf' Solovyev, 2026"
#define ABOUT_TEXT TITLE " v" VERSION "\n\nA simple text editor with powerful autocomplete.\n\n" COPYRIGHT
#define CAPABILITIES "AVX2 Q8 Q1/G QnK FlashAttn TurboQuant"
#define ARCHITECTURES "LLAMA QWEN3 BERT"
#define FONT_SIZE_DEFAULTS {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32}

#define NUMITEMS(X) (sizeof(X) / sizeof((X)[0]))
#define MIN(A,B) (((A) < (B))? (A) : (B))
#define MAX(A,B) (((A) > (B))? (A) : (B))
#define RGB_U32(R, G, B) (((uint32_t)(R) << 16) | ((uint32_t)(G) << 8) | (uint32_t)(B))
#define RGB_R(C) ((uchar)(((uint32_t)(C) >> 16) & 0xff))
#define RGB_G(C) ((uchar)(((uint32_t)(C) >> 8) & 0xff))
#define RGB_B(C) ((uchar)((uint32_t)(C) & 0xff))

std::string trim_newlines(std::string s);
std::string lowercase(std::string s);
bool is_word_char(char c);
std::string json_escape(const std::string &s);
std::string json_unescape(const std::string &s);
std::string sanitize_suggestion(const std::string &text, int max_chars);
int digit_count(int value);
int count_newlines(const char* text);
double now_seconds();
