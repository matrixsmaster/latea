#pragma once

#include <string>

#define HTTP_READ_BUFFER_SIZE 4096
#define HTTP_PORT_BUF_SIZE 32
#define HTTP_RETRY_WAIT_US 500000
#define HTTP_SRV_READY_POLLS 300
#define HTTP_SRV_READY_WAIT_US 100000
#define HTTP_MIN_TIMEOUT_MS 100

enum {
    LLAMA_ERR_NONE = 0,
    LLAMA_ERR_NO_TEXT,
    LLAMA_ERR_RESOLVE_HOST,
    LLAMA_ERR_CONNECT,
    LLAMA_ERR_SEND,
    LLAMA_ERR_TIMEOUT,
    LLAMA_ERR_READ,
    LLAMA_ERR_BAD_HTTP,
    LLAMA_ERR_BUSY,
    LLAMA_ERR_STATUS,
    LLAMA_ERR_BAD_PATH,
    LLAMA_ERR_START,
    LLAMA_ERR_EARLY_EXIT,
    LLAMA_ERR_NOT_READY,
    LLAMA_NUM_ERRORS
};

struct ai_request
{
    int serial, anchor_pos;
    std::string prefix, suffix;
    std::string system_prompt;
    std::string host, model_path, launch_path;
    int maxtoks, timeout_ms, context_length;
    float temperature, top_p;
    int top_k;
    int slot_id, port;
    bool cache_prompt, tquant, infill;
};

struct ai_result
{
    int serial, anchor_pos;
    std::string text, error;
};

struct llama_client
{
    static void stop_server();

    bool request_completion(const ai_request &req, ai_result &res);
    bool request_infill(const ai_request &req, ai_result &res);
    int ensure_server(const ai_request &req);

    int request_text(const ai_request &req, ai_result &res, const char* path, const std::string &json, int empty_err);
    int post_json(const std::string &host, int port, const std::string &path, const std::string &body, int timeout_ms, std::string &response_body);
    std::string build_completion_json(const ai_request &req) const;
    std::string build_infill_json(const ai_request &req) const;
    std::string parse_text_field(const std::string &json, const char* key) const;
};
