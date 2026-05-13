#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "common.h"

std::string trim_newlines(const std::string &s)
{
    std::string out = s;
    while (!out.empty() && (out[out.size() - 1] == '\n' || out[out.size() - 1] == '\r')) {
        out.erase(out.size() - 1, 1);
    }
    return out;
}

std::string lowercase(const std::string &s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

int is_word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

std::string json_escape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += (char)c;
        }
    }
    return out;
}

std::string json_unescape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        i++;
        if (s[i] == 'n') out += '\n';
        else if (s[i] == 'r') out += '\r';
        else if (s[i] == 't') out += '\t';
        else if (s[i] == '"' || s[i] == '\\' || s[i] == '/') out += s[i];
        else if (s[i] == 'u' && i + 4 < s.size()) {
            i += 4;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string sanitize_suggestion(const std::string &text, int max_chars)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n' || c == '\r') break;
        if (c < 32) {
            if (c == '\t') out += ' ';
            continue;
        }
        out += (char)c;
    }
    out = trim_newlines(out);
    while (!out.empty() && (out[0] == ' ' || out[0] == '\t')) out.erase(0, 1);
    if ((int)out.size() > max_chars) out.erase(max_chars);
    return out;
}

double now_seconds()
{
    using namespace std::chrono;
    return duration_cast<std::chrono::duration<double> >(std::chrono::steady_clock::now().time_since_epoch()).count();
}

