// Port of g4f/requests/__init__.py (StreamSession subset) on libcurl.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <functional>
#include <string>

#include "g4f/typing.hpp"

namespace g4f {

struct HttpResponse {
    long status = 0;
    std::string body;
    Headers headers;
};

using StreamChunk = std::function<bool(const char* data, size_t size)>;

class HttpClient {
public:
    explicit HttpClient(long timeout_s = 60);
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void set_headers(Headers headers) { headers_ = std::move(headers); }
    void set_cookies(const Cookies& cookies);
    void set_proxy(const std::string& proxy) { proxy_ = proxy; }
    void set_impersonate_chrome(bool v = true) { impersonate_chrome_ = v; }

    HttpResponse get(const std::string& url);
    HttpResponse post_json(const std::string& url, const std::string& json_body);
    // POST with streaming response body; chunk returns false to abort.
    HttpResponse post_stream(const std::string& url,
                             const std::string& json_body,
                             StreamChunk chunk);

private:
    struct Impl;
    Impl* impl_;
    Headers headers_;
    std::string proxy_;
    bool impersonate_chrome_ = true;
    long timeout_s_;
};

// Split an SSE/text stream into lines; calls emit(line) per '\n'-terminated
// line. Returns the trailing partial line (carry for the next chunk).
std::string feed_sse_lines(const std::string& data,
                           const std::function<void(const std::string&)>& emit);

} // namespace g4f
