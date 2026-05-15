#pragma once

#include <string>

#define HTTP_READ_BUFFER_SIZE 4096

struct ai_request
{
    int serial;
    int anchor_pos;
    std::string prefix;
    std::string suffix;
    std::string system_prompt;
    int endpoint_mode;
    int max_chars;
    int timeout_ms;
    int context_length;
    double temperature;
    double top_p;
    int top_k;
    int cache_prompt;
    int slot_id;
    std::string host;
    int port;
};

struct ai_result
{
    int serial;
    int anchor_pos;
    std::string text;
    std::string error;
};

struct llama_client
{
    bool request_completion(const ai_request &req, ai_result &res);
    bool request_infill(const ai_request &req, ai_result &res);

    bool post_json(const std::string &host, int port, const std::string &path, const std::string &body, int timeout_ms, std::string &response_body, std::string &error);
    std::string build_completion_json(const ai_request &req) const;
    std::string build_infill_json(const ai_request &req) const;
    std::string parse_text_field(const std::string &json, const char* key) const;
};
