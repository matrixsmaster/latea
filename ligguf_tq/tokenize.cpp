/* LiGGUF - a small, dependency-free LLaMA inference engine with direct GGUF support
 * (C) Dmitry 'sciloaf' Solovyev aka MatrixS_Master, 2025-2026
 * */

#include <map>
#include <ctype.h>
#include <string.h>
#include "common.h"
#include "tokenize.h"

using namespace std;

static string utf8_cp(int cp)
{
    string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

static const vector<string>& gpt2_byte_pieces()
{
    static vector<string> tab;
    if (!tab.empty()) return tab;

    vector<int> bs;
    for (int i = '!'; i <= '~'; i++) bs.push_back(i);
    for (int i = 0xA1; i <= 0xAC; i++) bs.push_back(i);
    for (int i = 0xAE; i <= 0xFF; i++) bs.push_back(i);

    vector<int> cs = bs;
    int n = 0;
    for (int b = 0; b < 256; b++) {
        bool found = false;
        for (int i = 0; i < (int)bs.size(); i++) if (bs[i] == b) found = true;
        if (found) continue;
        bs.push_back(b);
        cs.push_back(256 + n++);
    }

    tab.resize(256);
    for (int i = 0; i < 256; i++) tab[bs[i]] = utf8_cp(cs[i]);
    return tab;
}

static const map<string,unsigned char>& gpt2_piece_bytes()
{
    static map<string,unsigned char> rev;
    if (!rev.empty()) return rev;

    const vector<string>& tab = gpt2_byte_pieces();
    for (int i = 0; i < 256; i++) rev[tab[i]] = i;
    return rev;
}

static void tokenize_bpe_fallback(model_state* S, const char* str, vector<int> &out)
{
    while (*str) {
        string s;
        s += *str;
        if (S->m.tokens_rev.count(s)) out.push_back(S->m.tokens_rev.at(s));
        else {
            s = "<0x00>";
            snprintf(&s[0],s.length()+1,"<0x%02X>",*str);
            out.push_back(S->m.tokens_rev.at(s));
        }
        str++;
    }

    while (1) {
        float best_score = -1e10;
        unsigned best_id = -1;
        unsigned best_idx = out.size()+1;

        string acc;
        for (unsigned i = 0; i < out.size()-1; i++) {
            acc = S->m.tokens.at(out.at(i)) + S->m.tokens.at(out.at(i+1));
            if (!S->m.tokens_rev.count(acc)) continue;

            float sc = S->m.tokscores[S->m.tokens_rev[acc]];
            if (sc > best_score) {
                best_score = sc;
                best_id = S->m.tokens_rev[acc];
                best_idx = i;
            }
        }

        if (best_idx > out.size()) break;
        out[best_idx] = best_id;
        out.erase(out.begin()+best_idx+1);
    }
}

static void tokenize_merge_ranked(model_state* S, const char* str, vector<int> &out)
{
    vector<string> parts;
    const vector<string>& btab = gpt2_byte_pieces();

    while (*str) {
        unsigned char c = *str++;
        parts.push_back(btab[c]);
    }

    while (parts.size() > 1) {
        int best_rank = 1 << 30;
        int best_idx = -1;

        for (int i = 0; i < (int)parts.size()-1; i++) {
            string pair = parts[i] + " " + parts[i+1];
            if (!S->m.merge_rank.count(pair)) continue;
            int rank = S->m.merge_rank.at(pair);
            if (rank < best_rank) {
                best_rank = rank;
                best_idx = i;
            }
        }

        if (best_idx < 0) break;
        parts[best_idx] += parts[best_idx+1];
        parts.erase(parts.begin()+best_idx+1);
    }

    for (int i = 0; i < (int)parts.size(); i++) {
        if (S->m.tokens_rev.count(parts[i])) out.push_back(S->m.tokens_rev.at(parts[i]));
        else if (S->m.tok_unk) out.push_back(S->m.tok_unk);
    }
}

bool is_byte_token(const char* s)
{
    if (!s || s[0] != '<' || s[1] != '0' || s[2] != 'x' || s[5] != '>' || s[6]) return false;
    return isxdigit((unsigned char)s[3]) && isxdigit((unsigned char)s[4]);
}

static void tokenize_piece_slice(model_state* S, const char* str, int len, vector<int> &out)
{
    if (!str || len <= 0) return;
    char* slice = strndup(str,len);
    if (S->m.merge_rank.empty()) tokenize_bpe_fallback(S,slice,out);
    else tokenize_merge_ranked(S,slice,out);
    if (slice) free(slice);
}

static int match_special_token(model_state* S, const char* str)
{
    for (int i = 0; i < (int)S->m.special_tokens.size(); i++) {
        const string &tok = S->m.special_tokens[i];
        if (!strncmp(str,tok.c_str(),tok.length())) return S->m.tokens_rev.at(tok);
    }
    return -1;
}

static inline bool is_newline(unsigned char c)
{
    return c == '\n' || c == '\r';
}

static inline bool is_inline_space(unsigned char c)
{
    return isspace(c) && !is_newline(c);
}

static int match_apos_suffix(const char* s)
{
    if (!s || s[0] != '\'') return 0;

    char c0 = toupper(s[1]);
    char c1 = toupper(s[2]);

    if (strchr("SDMT",c0) && !isalpha(s[2])) return 2;
    if (c0 == 'L' && c1 == 'L' && !isalpha(s[3])) return 3;
    if ((c0 == 'R' || c0 == 'V') && c1 == 'E' && !isalpha(s[3])) return 3;
    return 0;
}

static inline bool splits_punct_run(const char* s)
{
    if (!*s) return true;
    return isalpha(*s) || isdigit(*s) || is_inline_space(*s) || is_newline(*s);
}

static inline bool splits_punct_run(model_state* S, const char* s)
{
    if (!*s) return true;
    if (match_special_token(S,s) >= 0) return true;
    return isalpha(*s) || isdigit(*s) || is_inline_space(*s) || is_newline(*s);
}

static void tokenize_qwen2(model_state* S, const char* str, vector<int> &out)
{
    while (*str) {
        int tok = match_special_token(S,str);
        if (tok >= 0) {
            out.push_back(tok);
            str += S->m.tokens[tok].length();
            continue;
        }

        const char* p = str;
        while (*p == ' ') p++;
        if (!*p) {
            tokenize_piece_slice(S,str,p - str,out);
            break;
        } else if (p > str + 1) {
            tokenize_piece_slice(S,str,p - str - 1,out);
            str = p - 1;
        } else if (is_inline_space(*str) && *str != ' ') {
            p = str;
            while (is_inline_space(*p) && *p != ' ') p++;
            tokenize_piece_slice(S,str,p - str,out);
            str = p;
        } else if (is_newline(*p)) {
            while (is_newline(*p)) p++;
            tokenize_piece_slice(S,str,p - str,out);
            str = p;
        } else if (isalpha(*p)) {
            p++;
            while (isalpha(*p)) p++;
            p += match_apos_suffix(p);
            tokenize_piece_slice(S,str,p - str,out);
            str = p;
        } else if (isdigit(*p)) {
            if (*str == ' ') {
                tokenize_piece_slice(S,str,1,out);
                str++;
                continue;
            }
            p = str;
            while (isdigit(*p) && p - str < 3) p++;
            tokenize_piece_slice(S,str,p - str,out);
            str = p;
        } else {
            p = str + (*str == ' ');
            if (!*p) {
                tokenize_piece_slice(S,str,1,out);
                break;
            }
            p++;
            while (*p && !splits_punct_run(S,p)) p++;
            tokenize_piece_slice(S,str,p - str,out);
            str = p;
        }
    }
}

vector<int> tokenize(model_state* S, const char* str, bool bos, bool eos)
{
    vector<int> out;
    out.reserve(strlen(str)+2);
    if (bos && S->m.add_bos && S->m.tok_bos > 0) out.push_back(S->m.tok_bos);

    if (S->m.tok_pre == "qwen2") tokenize_qwen2(S,str,out);
    else if (S->m.merge_rank.empty()) tokenize_bpe_fallback(S,str,out);
    else tokenize_merge_ranked(S,str,out);

    if (eos && S->m.tok_eos > 0) out.push_back(S->m.tok_eos);
    return out;
}

static string tok_to_str_gpt2(const string &src)
{
    const map<string,unsigned char>& rev = gpt2_piece_bytes();

    string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size();) {
        unsigned char c = src[i];
        size_t len = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < src.size()) len = 2;
        else if ((c & 0xF0) == 0xE0 && i + 2 < src.size()) len = 3;
        else if ((c & 0xF8) == 0xF0 && i + 3 < src.size()) len = 4;
        string cp = src.substr(i,len);
        if (rev.count(cp)) out += (char)rev.at(cp);
        else out += cp;
        i += len;
    }
    return out;
}

string tok_to_str(model_state* S, int tok)
{
    if (tok < 0 || tok >= S->m.vocab_size) {
        char s[64];
        snprintf(s,sizeof(s)," <Error token %d> ",tok);
        return s;
    }

    const string &src = S->m.tokens[tok];
    if (S->m.tok_model == "gpt2") return tok_to_str_gpt2(src);

    string out;
    out.reserve(src.size());

    for (size_t i = 0; i < src.size(); i++) {
        unsigned char c0 = src[i];
        if (c0 == 0xe2 && i + 2 < src.size()) {
            unsigned char c1 = src[i+1];
            unsigned char c2 = src[i+2];
            if (c1 == 0x96 && c2 == 0x81) {
                out += ' ';
                i += 2;
                continue;
            }
        }

        if (i + 5 < src.size() && is_byte_token(src.c_str() + i)) {
            int v = 0;
            if (sscanf(src.c_str() + i + 3,"%02X",&v) == 1) {
                out += (char)v;
                i += 5;
                continue;
            }
        }

        out += src[i];
    }
    return out;
}
