// Port of DeepSeek.iter_chat_stream (Provider/needs_auth/DeepSeek.py):
// consumes one logical answer across completion / resume / continue calls.
// Transport is injected so the loop is testable offline with canned streams.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "g4f/providers/stream.hpp"
#include "g4f/typing.hpp"

namespace g4f::deepseek {

struct HttpExchange {
    long status = 0;
    std::string content_type;
    std::string body;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual HttpExchange post(const std::string& url,
                              const std::string& json_body,
                              const Headers& headers) = 0;
};

// Visible driver output. Port of yielded `str` / `Reasoning` / `FinishReason`.
struct DriverEvent {
    bool finish = false;
    bool reasoning = false;
    std::string text;   // chunk text (finish=false)
    std::string reason; // finish reason (finish=true)
};

struct DriverOptions {
    bool auto_continue = true;
    // nullopt = unlimited (port: None).
    std::optional<int> max_continue_attempts = 20;
    std::optional<int> max_resume_attempts = 5;
};

// Port of iter_chat_stream. initial_payload must carry chat_session_id
// (port of the ValueError guard). Throws ResponseError/RuntimeError with the
// upstream messages on the same conditions.
std::vector<DriverEvent> run_chat_stream(ITransport& transport,
                                         const nlohmann::json& initial_payload,
                                         const Headers& initial_headers,
                                         StreamConversation& conv,
                                         const DriverOptions& options = DriverOptions());

} // namespace g4f::deepseek
