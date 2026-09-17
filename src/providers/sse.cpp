// Port of iter_deepseek_sse.
#include "g4f/providers/sse.hpp"

namespace g4f::deepseek {

namespace {
std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}
} // namespace

std::optional<SseFrame> SseFrameParser::decode_event() {
    if (data_lines_.empty()) return std::nullopt;
    std::string raw = join_lines(data_lines_);
    std::string trimmed = raw;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed == "[DONE]") return std::nullopt;
    try {
        return SseFrame{event_type_, nlohmann::json::parse(raw)};
    } catch (const nlohmann::json::parse_error&) {
        throw std::runtime_error("Invalid DeepSeek SSE JSON data: '" + raw + "'");
    }
}

std::vector<SseFrame> SseFrameParser::feed_line(const std::string& raw_line) {
    std::string line = raw_line;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty()) {
        std::vector<SseFrame> out;
        if (auto ev = decode_event()) out.push_back(*ev);
        event_type_ = "message";
        data_lines_.clear();
        return out;
    }
    if (line[0] == ':') return {}; // comment/heartbeat
    auto pos = line.find(':');
    if (pos == std::string::npos) return {};
    std::string name = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
    if (name == "event") {
        event_type_ = value.empty() ? "message" : value;
    } else if (name == "data") {
        data_lines_.push_back(value);
    }
    return {};
}

std::vector<SseFrame> SseFrameParser::flush() {
    std::vector<SseFrame> out;
    if (auto ev = decode_event()) out.push_back(*ev);
    event_type_ = "message";
    data_lines_.clear();
    return out;
}

} // namespace g4f::deepseek
