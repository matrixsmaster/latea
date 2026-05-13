#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "common.h"
#include "llama_client.h"

static const size_t HTTP_READ_BUFFER_SIZE = 4096;

bool llama_client::request_completion(const ai_request &req, ai_result &res)
{
    std::string body;
    std::string error;
    if (!post_json(req.host, req.port, "/completion", build_completion_json(req),
            req.timeout_ms, body, error)) {
        res.error = error;
        return false;
    }
    res.text = parse_text_field(body, "content");
    if (res.text.empty()) res.text = parse_text_field(body, "completion");
    if (res.text.empty()) {
        res.error = "llama.cpp returned no completion text";
        return false;
    }
    return true;
}

bool llama_client::request_infill(const ai_request &req, ai_result &res)
{
    std::string body;
    std::string error;
    if (!post_json(req.host, req.port, "/infill", build_infill_json(req),
            req.timeout_ms, body, error)) {
        res.error = error;
        return false;
    }
    res.text = parse_text_field(body, "content");
    if (res.text.empty()) res.text = parse_text_field(body, "completion");
    if (res.text.empty()) {
        res.error = "llama.cpp returned no infill text";
        return false;
    }
    return true;
}

std::string llama_client::build_completion_json(const ai_request &req) const
{
    std::ostringstream out;
    std::string prompt;
    if (!req.system_prompt.empty()) prompt = req.system_prompt + "\n\n";
    prompt += req.prefix;
    out << "{";
    out << "\"prompt\":\"" << json_escape(prompt) << "\",";
    out << "\"n_predict\":" << req.max_chars << ",";
    out << "\"temperature\":" << req.temperature << ",";
    out << "\"top_p\":" << req.top_p << ",";
    out << "\"cache_prompt\":" << (req.cache_prompt ? "true" : "false") << ",";
    out << "\"id_slot\":" << req.slot_id;
    out << "}";
    return out.str();
}

std::string llama_client::build_infill_json(const ai_request &req) const
{
    std::ostringstream out;
    std::string prefix = req.prefix;
    if (!req.system_prompt.empty()) prefix = req.system_prompt + "\n\n" + prefix;
    out << "{";
    out << "\"input_prefix\":\"" << json_escape(prefix) << "\",";
    out << "\"input_suffix\":\"" << json_escape(req.suffix) << "\",";
    out << "\"n_predict\":" << req.max_chars << ",";
    out << "\"temperature\":" << req.temperature << ",";
    out << "\"top_p\":" << req.top_p << ",";
    out << "\"cache_prompt\":" << (req.cache_prompt ? "true" : "false") << ",";
    out << "\"id_slot\":" << req.slot_id;
    out << "}";
    return out.str();
}

std::string llama_client::parse_text_field(const std::string &json, const char *key) const
{
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;

    std::string value;
    int escaped = 0;
    for (; pos < json.size(); pos++) {
        char c = json[pos];
        if (!escaped && c == '"') break;
        if (!escaped && c == '\\') {
            escaped = 1;
            value += c;
            continue;
        }
        escaped = 0;
        value += c;
    }
    return json_unescape(value);
}

bool llama_client::post_json(const std::string &host, int port, const std::string &path,
    const std::string &body, int timeout_ms, std::string &response_body,
    std::string &error)
{
    char port_buf[32];
    snprintf(port_buf, sizeof(port_buf), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = 0;
    if (getaddrinfo(host.c_str(), port_buf, &hints, &res) != 0) {
        error = "cannot resolve AI host";
        return false;
    }

    int fd = -1;
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        error = "cannot connect to llama.cpp server";
        return false;
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Connection: close\r\n";
    request << "Content-Length: " << body.size() << "\r\n\r\n";
    request << body;
    std::string req_text = request.str();

    size_t sent = 0;
    while (sent < req_text.size()) {
        ssize_t rc = send(fd, req_text.data() + sent, req_text.size() - sent, 0);
        if (rc <= 0) {
            close(fd);
            error = "failed to send AI request";
            return false;
        }
        sent += (size_t)rc;
    }

    std::string raw;
    char buf[HTTP_READ_BUFFER_SIZE];
    for (;;) {
        ssize_t rc = recv(fd, buf, sizeof(buf), 0);
        if (rc == 0) break;
        if (rc < 0) {
            close(fd);
            error = "failed to read AI response";
            return false;
        }
        raw.append(buf, (size_t)rc);
    }
    close(fd);

    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error = "bad HTTP response from AI server";
        return false;
    }

    std::string header = raw.substr(0, header_end);
    response_body = raw.substr(header_end + 4);
    if (header.find("200") == std::string::npos) {
        error = "AI server returned non-200 status";
        return false;
    }
    return true;
}
