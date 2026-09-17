// Port of needs_auth/deepseek/stream.py (state machine + payload routing).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace g4f::deepseek {

// Port of DEEPSEEK_MESSAGE_STATUSES / DEEPSEEK_FINISH_REASONS.
bool is_message_status(const std::string& status);
std::string finish_reason_for(const std::string& status); // "" if unknown

// Port of _fragment_kind: "response" | "reasoning" | nullopt(ignored).
std::optional<std::string> fragment_kind(const nlohmann::json& fragment);

// Visible output piece. Port of `Reasoning(content)` vs plain `str` chunks.
struct Chunk {
    bool reasoning = false; // true ~ Reasoning(...), false ~ plain text
    std::string text;
};

// Port of _DeepSeekStreamState.
struct StreamState {
    nlohmann::json message_id = nullptr;
    std::string status;
    bool closed = false;
    std::optional<std::string> active_kind = std::string("response");
    std::map<std::string, std::optional<std::string>> fragment_kinds;
    long long next_fragment_index = 0;
    std::map<std::string, std::string> emitted{{"reasoning", ""}, {"response", ""}};

    std::string append(const std::string& kind, const std::string& content);
    std::string snapshot_delta(const std::string& kind, const std::string& content);
};

// Minimal conversation bits touched by the stream layer
// (port: JsonConversation.parent_message_id).
struct StreamConversation {
    nlohmann::json parent_message_id = nullptr;
};

// Port of _process_fragments. snapshot=true clears kind tracking first.
std::vector<Chunk> process_fragments(const nlohmann::json& fragments,
                                     StreamState& state,
                                     bool snapshot);

// Port of _process_stream_payload. Returns newly visible chunks.
std::vector<Chunk> process_stream_payload(const nlohmann::json& payload,
                                          StreamState& state,
                                          StreamConversation& conv);

// Port of _process_full_message (resume path).
std::vector<Chunk> process_full_message(const nlohmann::json& biz_data,
                                        StreamState& state,
                                        StreamConversation& conv);

} // namespace g4f::deepseek
