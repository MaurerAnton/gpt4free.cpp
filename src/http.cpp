// Port of g4f/requests StreamSession subset on libcurl.
#include "g4f/http.hpp"

#include <curl/curl.h>

#include <sstream>

namespace g4f {

struct HttpClient::Impl {
    CURL* curl = nullptr;
};

namespace {
size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}
size_t write_header(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<Headers*>(userdata);
    std::string line(ptr, size * nmemb);
    auto pos = line.find(':');
    if (pos != std::string::npos) {
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
            value.pop_back();
        if (!key.empty()) (*headers)[key] = value;
    }
    return size * nmemb;
}
struct StreamCtx {
    HttpResponse* res;
    StreamChunk chunk;
};
size_t write_stream(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamCtx*>(userdata);
    ctx->res->body.append(ptr, size * nmemb);
    if (ctx->chunk && !ctx->chunk(ptr, size * nmemb)) return 0; // abort
    return size * nmemb;
}
} // namespace

HttpClient::HttpClient(long timeout_s) : impl_(new Impl), timeout_s_(timeout_s) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    impl_->curl = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (impl_->curl) curl_easy_cleanup(impl_->curl);
    delete impl_;
}

void HttpClient::set_cookies(const Cookies& cookies) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [k, v] : cookies) {
        if (!first) oss << "; ";
        oss << k << "=" << v;
        first = false;
    }
    headers_["Cookie"] = oss.str();
}

namespace {
struct Request {
    CURL* curl;
    struct curl_slist* list = nullptr;
    ~Request() { if (list) curl_slist_free_all(list); }
};
Request prepare(CURL* curl, const std::string& url, const Headers& headers,
                const std::string& proxy, bool chrome, long timeout_s) {
    Request req{curl};
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
    if (chrome) {
        // Port of impersonate="chrome": Chrome-like TLS/HTTP2 fingerprint
        // approximation (libcurl cannot fully impersonate; curl-impersonate
        // builds can replace this client later).
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
        req.list = curl_slist_append(req.list,
            "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36");
    }
    for (const auto& [k, v] : headers)
        req.list = curl_slist_append(req.list, (k + ": " + v).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req.list);
    return req;
}
} // namespace

HttpResponse HttpClient::get(const std::string& url) {
    HttpResponse res;
    Request req = prepare(impl_->curl, url, headers_, proxy_, impersonate_chrome_, timeout_s_);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERDATA, &res.headers);
    curl_easy_perform(impl_->curl);
    curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &res.status);
    return res;
}

HttpResponse HttpClient::post_json(const std::string& url, const std::string& json_body) {
    HttpResponse res;
    Headers h = headers_;
    h["Content-Type"] = "application/json";
    Request req = prepare(impl_->curl, url, h, proxy_, impersonate_chrome_, timeout_s_);
    curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, (long)json_body.size());
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(impl_->curl, CURLOPT_HEADERDATA, &res.headers);
    curl_easy_perform(impl_->curl);
    curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &res.status);
    return res;
}

HttpResponse HttpClient::post_stream(const std::string& url,
                                     const std::string& json_body,
                                     StreamChunk chunk) {
    HttpResponse res;
    Headers h = headers_;
    h["Content-Type"] = "application/json";
    h["Accept"] = "text/event-stream";
    h["Cache-Control"] = "no-cache";
    Request req = prepare(impl_->curl, url, h, proxy_, impersonate_chrome_, timeout_s_);
    StreamCtx ctx{&res, std::move(chunk)};
    curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, (long)json_body.size());
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, write_stream);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_perform(impl_->curl);
    curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &res.status);
    return res;
}

std::string feed_sse_lines(const std::string& data,
                           const std::function<void(const std::string&)>& emit) {
    size_t start = 0;
    while (true) {
        size_t pos = data.find('\n', start);
        if (pos == std::string::npos) return data.substr(start);
        std::string line = data.substr(start, pos - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        emit(line);
        start = pos + 1;
    }
}

} // namespace g4f
