#ifndef LATEA_COMMON_H
#define LATEA_COMMON_H

#include <string>

std::string trim_newlines(const std::string &s);
std::string lowercase(const std::string &s);
int is_word_char(char c);
std::string json_escape(const std::string &s);
std::string json_unescape(const std::string &s);
std::string sanitize_suggestion(const std::string &text, int max_chars);
double now_seconds();

#endif
