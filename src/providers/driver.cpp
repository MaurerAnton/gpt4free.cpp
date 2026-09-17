// Port of DeepSeek.iter_chat_stream.
#include "g4f/providers/driver.hpp"

#include "g4f/errors.hpp"
#include "g4f/providers/deepseek.hpp"
#include "g4f/providers/envelope.hpp"
#include "g4f/providers/sse.hpp"

namespace g4f::deepseek {
namespace {

bool has_lower(const std::string& haystack, const std::string& needle) {
    std::string h = haystack;
    for (auto& c : h) c = (char)std::tolower((unsigned char)c);
    return h.find(needle) != std::string::npos;
}

void push_chunks(std::vector<DriverEvent>& out, const std::vector<Chunk>& chunks) {
    for (const auto& c : chunks)
        out.push_back(DriverEvent{false, c.reasoning, c.text, ""});
}

} // namespace

std::vector<DriverEvent> run_chat_stream(ITransport& transport,
                                         const nlohmann::json& initial_payload,
                                         const Headers& initial_headers,
                                         StreamConversation& conv,
                                         const DriverOptions& options) {
    for (const auto& [name, limit] :
         {std::pair<const char*, std::optional<int>>{"max_continue_attempts", options.max_continue_attempts},
          {"max_resume_attempts", options.max_resume_attempts}}) {
        if (limit.has_value() && *limit < 0)
            throw std::invalid_argument(std::string(name) + " must be non-negative");
    }

    std::string chat_session_id = initial_payload.value("chat_session_id", "");
    if (chat_session_id.empty())
        throw std::invalid_argument("DeepSeek chat_session_id is required for streaming");

    std::vector<DriverEvent> out;
    std::string endpoint = CHAT_COMPLETION_ENDPOINT;
    nlohmann::json payload = initial_payload;
    Headers request_headers = initial_headers;
    StreamState state;
    int continue_attempts = 0;
    int resume_attempts = 0;
    bool empty_response_resume_attempted = false;

    while (true) {
        state.closed = false;
        nlohmann::json close_payload = nlohmann::json::object();

        HttpExchange ex = transport.post(endpoint, payload.dump(), request_headers);

        if (!has_lower(ex.content_type, "text/event-stream")) {
            nlohmann::json result = nlohmann::json::parse(
                ex.body.empty() ? "null" : ex.body);
            BizResponse biz = unwrap_biz_response(result, "chat stream", {"22"});
            if (biz.code == 22 || biz.code == "22") {
                for (auto&& c : process_full_message(biz.biz_data, state, conv))
                    push_chunks(out, {c});
                if (state.emitted["response"].empty())
                    throw ResponseError("DeepSeek finished without a response");
                std::string fr = finish_reason_for(state.status);
                if (!fr.empty()) out.push_back(DriverEvent{true, false, "", fr});
                return out;
            }
            throw std::runtime_error("Expected SSE response but got content-type: " +
                                     (ex.content_type.empty() ? "unknown" : ex.content_type));
        }

        SseFrameParser parser;
        std::string carry;
        size_t pos = 0;
        auto emit_line = [&](const std::string& line) {
            for (auto&& f : parser.feed_line(line)) {
                if (f.first == "close") {
                    state.closed = true;
                    if (f.second.is_object()) close_payload = f.second;
                } else if (f.first == "message" || f.first == "ready") {
                    push_chunks(out, process_stream_payload(f.second, state, conv));
                }
            }
        };
        while (true) {
            size_t nl = ex.body.find('\n', pos);
            if (nl == std::string::npos) break;
            emit_line(carry + ex.body.substr(pos, nl - pos));
            carry.clear();
            pos = nl + 1;
        }
        carry += ex.body.substr(pos);
        for (auto&& f : parser.flush()) {
            if (f.first == "close") {
                state.closed = true;
                if (f.second.is_object()) close_payload = f.second;
            } else if (f.first == "message" || f.first == "ready") {
                push_chunks(out, process_stream_payload(f.second, state, conv));
            }
        }

        if (state.closed) {
            resume_attempts = 0;
            bool should_continue = state.status == "INCOMPLETE" && options.auto_continue;

            if (state.status == "FINISHED" && state.emitted["response"].empty()) {
                bool can_resume = !state.message_id.is_null() &&
                                  !empty_response_resume_attempted &&
                                  (!options.max_resume_attempts.has_value() ||
                                   resume_attempts < *options.max_resume_attempts);
                if (can_resume) {
                    empty_response_resume_attempted = true;
                    resume_attempts += 1;
                    endpoint = CHAT_SESSION_RESUME_STREAM_ENDPOINT;
                    payload = {{"chat_session_id", chat_session_id},
                               {"message_id", state.message_id}};
                    request_headers.clear();
                    continue;
                }
                throw ResponseError("DeepSeek finished without a response");
            }
            if (empty_response_resume_attempted && state.emitted["response"].empty() &&
                !should_continue) {
                throw ResponseError("DeepSeek finished without a response");
            }
            if (!should_continue) {
                std::string fr = finish_reason_for(state.status);
                if (!fr.empty()) out.push_back(DriverEvent{true, false, "", fr});
                return out;
            }
            if (state.message_id.is_null()) {
                throw std::runtime_error(
                    "DeepSeek closed an incomplete stream without a message_id");
            }
            if (options.max_continue_attempts.has_value() &&
                continue_attempts >= *options.max_continue_attempts) {
                throw std::runtime_error("DeepSeek response remained INCOMPLETE after " +
                                         std::to_string(continue_attempts) +
                                         " continue attempt(s)");
            }
            continue_attempts += 1;
            endpoint = CHAT_SESSION_CONTINUE_ENDPOINT;
            payload = {{"chat_session_id", chat_session_id},
                       {"message_id", state.message_id},
                       {"fallback_to_resume", true}};
            request_headers.clear();
            state.status.clear();
            continue;
        }

        // Stream ended without close: resume when we have a message id.
        if (state.message_id.is_null()) {
            throw std::runtime_error("DeepSeek stream ended without close or message_id");
        }
        if (options.max_resume_attempts.has_value() &&
            resume_attempts >= *options.max_resume_attempts) {
            throw std::runtime_error("DeepSeek stream did not close normally after " +
                                     std::to_string(resume_attempts) + " resume attempt(s)");
        }
        resume_attempts += 1;
        endpoint = CHAT_SESSION_RESUME_STREAM_ENDPOINT;
        payload = {{"chat_session_id", chat_session_id},
                   {"message_id", state.message_id}};
        request_headers.clear();
    }
}

} // namespace g4f::deepseek
