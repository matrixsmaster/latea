#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include "common.h"
#include "llama_client.h"

static const char* const llama_errtab[LLAMA_NUM_ERRORS] = {
    "",
    "llama.cpp returned no text",
    "cannot resolve AI host",
    "cannot connect to llama.cpp server",
    "failed to send AI request",
    "AI server timed out",
    "failed to read AI response",
    "bad HTTP response from AI server",
    "AI server busy",
    "AI server returned non-200 status",
    "llama.cpp path is invalid",
    "cannot start llama.cpp server",
    "llama.cpp server exited early",
    "llama.cpp server did not become ready",
};

static int srv_pid = 0;

static bool can_launch_local(const ai_request &req)
{
    return !req.model_path.empty() && !req.launch_path.empty();
}

static bool can_connect_port(const std::string &host, int port)
{
    char port_buf[HTTP_PORT_BUF_SIZE];
    struct addrinfo hints;
    struct addrinfo* res = 0;
    int fd = -1;
    bool ok = false;

    snprintf(port_buf, sizeof(port_buf), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port_buf, &hints, &res) != 0) return false;

    for (struct addrinfo* it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            ok = true;
            close(fd);
            break;
        }
        close(fd);
    }
    freeaddrinfo(res);
    return ok;
}

void llama_client::stop_server()
{
    if (srv_pid <= 0) return;
    kill(srv_pid, SIGTERM);
    waitpid(srv_pid, NULL, 0);
    srv_pid = 0;
}

bool llama_client::request_completion(const ai_request &req, ai_result &res)
{
    return request_text(req, res, "/completion", build_completion_json(req), LLAMA_ERR_NO_TEXT);
}

bool llama_client::request_infill(const ai_request &req, ai_result &res)
{
    return request_text(req, res, "/infill", build_infill_json(req), LLAMA_ERR_NO_TEXT);
}

bool llama_client::request_text(const ai_request &req, ai_result &res, const char* path, const std::string &json, int empty_err)
{
    std::string body;
    int error = LLAMA_ERR_NONE;
    int retries = MAX(1, req.timeout_ms * 1000 / HTTP_RETRY_WAIT_US);

    for (int i = 0; i < retries; i++) {
        if (!ensure_server(req, error)) {
            res.error = llama_errtab[error];
            return false;
        }
        error = post_json(req.host, req.port, path, json, req.timeout_ms, body);
        if (error == LLAMA_ERR_NONE) break;
        if (error != LLAMA_ERR_BUSY && (!can_launch_local(req) || error != LLAMA_ERR_CONNECT)) {
            res.error = llama_errtab[error];
            return false;
        }
        if (i + 1 >= retries) break;
        usleep(HTTP_RETRY_WAIT_US);
    }
    if (body.empty()) {
        if (error == LLAMA_ERR_NONE) error = LLAMA_ERR_CONNECT;
        res.error = llama_errtab[error];
        return false;
    }
    res.text = parse_text_field(body, "content");
    if (res.text.empty()) res.text = parse_text_field(body, "completion");
    if (res.text.empty()) {
        res.error = llama_errtab[empty_err];
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
    out << "\"n_ctx\":" << req.context_length << ",";
    out << "\"temperature\":" << req.temperature << ",";
    out << "\"top_p\":" << req.top_p << ",";
    out << "\"top_k\":" << req.top_k << ",";
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
    out << "\"n_ctx\":" << req.context_length << ",";
    out << "\"temperature\":" << req.temperature << ",";
    out << "\"top_p\":" << req.top_p << ",";
    out << "\"top_k\":" << req.top_k << ",";
    out << "\"cache_prompt\":" << (req.cache_prompt ? "true" : "false") << ",";
    out << "\"id_slot\":" << req.slot_id;
    out << "}";
    return out.str();
}

std::string llama_client::parse_text_field(const std::string &json, const char* key) const
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

bool llama_client::ensure_server(const ai_request &req, int &error)
{
    int status = 0;
    struct stat st;
    std::string path = req.launch_path;

    if (!can_launch_local(req)) return true;
    if (srv_pid > 0 && waitpid(srv_pid, &status, WNOHANG) == srv_pid) srv_pid = 0;
    if (srv_pid > 0) return true;
    if (!stat(path.c_str(), &st) && S_ISDIR(st.st_mode)) path += "/llama-server";
    if (access(path.c_str(), X_OK) != 0) {
        error = LLAMA_ERR_BAD_PATH;
        return false;
    }

    srv_pid = fork();
    if (srv_pid < 0) {
        srv_pid = 0;
        error = LLAMA_ERR_START;
        return false;
    }
    if (!srv_pid) {
        char port[32];
        char ctx[32];
        char slots[32];

        snprintf(port, sizeof(port), "%d", req.port);
        snprintf(ctx, sizeof(ctx), "%d", req.context_length);
        snprintf(slots, sizeof(slots), "%d", MAX(1, req.slot_id + 1));
        execl(path.c_str(), path.c_str(), "-m", req.model_path.c_str(), "--port", port, "-c", ctx, "--host", req.host.c_str(), "--slots", "--parallel", slots, (char*)NULL);
        _exit(127);
    }

    for (int i = 0; i < HTTP_SRV_READY_POLLS; i++) {
        if (waitpid(srv_pid, &status, WNOHANG) == srv_pid) {
            srv_pid = 0;
            error = LLAMA_ERR_EARLY_EXIT;
            return false;
        }
        if (can_connect_port(req.host, req.port)) return true;
        usleep(HTTP_SRV_READY_WAIT_US);
    }

    error = LLAMA_ERR_NOT_READY;
    stop_server();
    return false;
}

int llama_client::post_json(const std::string &host, int port, const std::string &path, const std::string &body, int timeout_ms, std::string &response_body)
{
    char port_buf[HTTP_PORT_BUF_SIZE];
    snprintf(port_buf, sizeof(port_buf), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = 0;
    if (getaddrinfo(host.c_str(), port_buf, &hints, &res) != 0) return LLAMA_ERR_RESOLVE_HOST;

    int fd = -1;
    for (struct addrinfo* it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        int ms = MAX(timeout_ms, HTTP_MIN_TIMEOUT_MS);
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) return LLAMA_ERR_CONNECT;

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
            return LLAMA_ERR_SEND;
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
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? LLAMA_ERR_TIMEOUT : LLAMA_ERR_READ;
        }
        raw.append(buf, (size_t)rc);
    }
    close(fd);

    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return LLAMA_ERR_BAD_HTTP;

    std::string header = raw.substr(0, header_end);
    response_body = raw.substr(header_end + 4);
    if (header.find("200") == std::string::npos) {
        if (header.find("503") != std::string::npos) return LLAMA_ERR_BUSY;
        return LLAMA_ERR_STATUS;
    }

    return LLAMA_ERR_NONE;
}
