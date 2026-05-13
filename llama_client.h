#ifndef LATEA_LLAMA_CLIENT_H
#define LATEA_LLAMA_CLIENT_H

#include <string>

struct ai_request {
    int serial;
    int anchor_pos;
    std::string prefix;
    std::string suffix;
    std::string system_prompt;
    int endpoint_mode;
    int max_chars;
    int timeout_ms;
    double temperature;
    double top_p;
    int cache_prompt;
    int slot_id;
    std::string host;
    int port;
};

struct ai_result {
    int serial;
    int anchor_pos;
    std::string text;
    std::string error;
};

class llama_client {
public:
    bool request_completion(const ai_request &req, ai_result &res);
    bool request_infill(const ai_request &req, ai_result &res);

private:
    bool post_json(const std::string &host, int port, const std::string &path,
        const std::string &body, int timeout_ms, std::string &response_body,
        std::string &error);
    std::string build_completion_json(const ai_request &req) const;
    std::string build_infill_json(const ai_request &req) const;
    std::string parse_text_field(const std::string &json, const char *key) const;
};

#endif
