#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "common.h"

using namespace std;

string trim_newlines(const string &s)
{
    string out = s;
    while (!out.empty() && (out[out.size() - 1] == '\n' || out[out.size() - 1] == '\r')) {
        out.erase(out.size() - 1, 1);
    }
    return out;
}

string lowercase(const string &s)
{
    string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

bool is_word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

string json_escape(const string &s)
{
    string out;
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

string json_unescape(const string &s)
{
    string out;
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

string sanitize_suggestion(const string &text, int max_chars)
{
    string out;
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
    if (max_chars > 0 && (int)out.size() > max_chars) out.erase(max_chars);
    return out;
}

int digit_count(int value)
{
    int digits = 1;
    for (int n = value; n >= 10; n /= 10) digits++;
    return digits;
}

int count_newlines(const char* text)
{
    if (!text) return 0;
    int count = 0;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') count++;
    }
    return count;
}

double now_seconds()
{
    using namespace chrono;
    return duration_cast<chrono::duration<double> >(chrono::steady_clock::now().time_since_epoch()).count();
}
