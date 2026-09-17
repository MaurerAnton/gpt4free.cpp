// Port of iter_deepseek_sse in needs_auth/deepseek/stream.py:
// DeepSeek SSE frames keep their `event` field; data lines accumulate.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace g4f::deepseek {

// One parsed frame: (event_type, data_json). "[DONE]" terminates (nullopt).
using SseFrame = std::pair<std::string, nlohmann::json>;

class SseFrameParser {
public:
    // Feed one raw line (without trailing '\n'); returns frames completed
    // by blank-line separators (usually 0 or 1).
    std::vector<SseFrame> feed_line(const std::string& raw_line);
    // Flush any trailing buffered frame (port: final decode_event()).
    std::vector<SseFrame> flush();

private:
    std::optional<SseFrame> decode_event();
    std::string event_type_ = "message";
    std::vector<std::string> data_lines_;
};

} // namespace g4f::deepseek
