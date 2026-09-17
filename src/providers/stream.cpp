// Port of needs_auth/deepseek/stream.py.
#include "g4f/providers/stream.hpp"

#include <cctype>
#include <algorithm>

namespace g4f::deepseek {
namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

bool in_set(const std::string& v, const char* const* arr, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (v == arr[i]) return true;
    return false;
}

std::optional<Chunk> stream_output(const std::string& kind, const std::string& content) {
    if (content.empty()) return std::nullopt;
    return Chunk{kind == "reasoning", content};
}

void record_fragment_kind(StreamState& state, const std::string& index,
                          const std::optional<std::string>& kind, bool append_fragment = false) {
    state.active_kind = kind;
    if (index == "-1") {
        state.fragment_kinds["-1"] = kind;
        if (append_fragment) {
            state.fragment_kinds[std::to_string(state.next_fragment_index)] = kind;
            state.next_fragment_index += 1;
        } else if (state.next_fragment_index) {
            state.fragment_kinds[std::to_string(state.next_fragment_index - 1)] = kind;
        }
        return;
    }
    state.fragment_kinds[index] = kind;
    try {
        long long numeric = std::stoll(index);
        if (numeric >= state.next_fragment_index)
            state.next_fragment_index = numeric + 1;
        if (numeric == state.next_fragment_index - 1)
            state.fragment_kinds["-1"] = kind;
    } catch (...) {
    }
}

std::optional<std::string> fragment_index_from_path(const std::string& path) {
    // response/fragments/<i>
    const std::string pre = "response/fragments/";
    if (path.rfind(pre, 0) == 0 && path.size() > pre.size() &&
        path.find('/', pre.size()) == std::string::npos)
        return path.substr(pre.size());
    return std::nullopt;
}

void record_message_id(StreamState& state, StreamConversation& conv,
                       const nlohmann::json& message_id) {
    if (!message_id.is_null()) {
        state.message_id = message_id;
        conv.parent_message_id = message_id;
    }
}

// Returns true when the path is a status path (handled either way).
bool record_stream_status(StreamState& state, const std::string& path,
                          const nlohmann::json& value) {
    if (path != "response/status" && path != "quasi_status" &&
        path != "response/quasi_status")
        return false;
    std::string status = value.is_string() ? upper(value.get<std::string>()) : "";
    if (is_message_status(status)) state.status = status;
    return true;
}

} // namespace

bool is_message_status(const std::string& status) {
    static const char* k[] = {"FINISHED", "CONTENT_FILTER", "CONTEXT_LENGTH_EXCEEDED",
                              "INCOMPLETE", "WIP", "TIMEOUT"};
    return in_set(status, k, 6);
}

std::string finish_reason_for(const std::string& status) {
    if (status == "FINISHED") return "stop";
    if (status == "CONTENT_FILTER") return "content_filter";
    if (status == "CONTEXT_LENGTH_EXCEEDED") return "length";
    if (status == "INCOMPLETE") return "incomplete";
    if (status == "WIP") return "wip";
    if (status == "TIMEOUT") return "timeout";
    return "";
}

std::optional<std::string> fragment_kind(const nlohmann::json& fragment) {
    std::string t = upper(fragment.value("type", std::string("")));
    if (t.empty()) return std::string("response");
    static const char* resp[] = {"RESPONSE", "TEMPLATE_RESPONSE"};
    static const char* reas[] = {"THINK", "THINKING", "REASONING", "SEARCH",
                                 "TOOL_SEARCH", "TOOL_OPEN", "TOOL_FIND"};
    if (in_set(t, resp, 2)) return std::string("response");
    if (in_set(t, reas, 7)) return std::string("reasoning");
    return std::nullopt;
}

std::string StreamState::append(const std::string& kind, const std::string& content) {
    emitted[kind] += content;
    return content;
}

std::string StreamState::snapshot_delta(const std::string& kind,
                                        const std::string& content) {
    std::string& previous = emitted[kind]; // port: defaultdict-like via map[]
    if (content.rfind(previous, 0) == 0) { // content.startswith(previous)
        std::string delta = content.substr(previous.size());
        emitted[kind] = content;
        return delta;
    }
    if (previous.rfind(content, 0) == 0) return "";
    size_t max_overlap = std::min(previous.size(), content.size());
    for (size_t overlap = max_overlap; overlap > 0; --overlap) {
        if (previous.compare(previous.size() - overlap, overlap, content, 0, overlap) == 0) {
            std::string delta = content.substr(overlap);
            emitted[kind] += delta;
            return delta;
        }
    }
    emitted[kind] += content;
    return content;
}

std::vector<Chunk> process_fragments(const nlohmann::json& fragments,
                                     StreamState& state,
                                     bool snapshot) {
    std::vector<Chunk> chunks;
    std::map<std::string, std::string> content_by_kind{{"reasoning", ""}, {"response", ""}};
    std::vector<std::string> kind_order;

    if (snapshot) {
        state.fragment_kinds.clear();
        state.next_fragment_index = 0;
    }
    if (!fragments.is_array()) return chunks;

    for (const auto& fragment : fragments) {
        if (!fragment.is_object()) continue;
        auto kind = fragment_kind(fragment);
        std::string index = std::to_string(state.next_fragment_index);
        record_fragment_kind(state, index, kind, false);
        if (!kind.has_value()) continue;
        const auto content = fragment.value("content", nlohmann::json(nullptr));
        if (!content.is_string()) continue;
        if (snapshot) {
            if (std::find(kind_order.begin(), kind_order.end(), *kind) == kind_order.end())
                kind_order.push_back(*kind);
            content_by_kind[*kind] += content.get<std::string>();
            continue;
        }
        if (auto out = stream_output(*kind, state.append(*kind, content.get<std::string>())))
            chunks.push_back(*out);
    }

    if (snapshot) {
        for (const auto& kind : kind_order) {
            if (auto out = stream_output(kind, state.snapshot_delta(kind, content_by_kind[kind])))
                chunks.push_back(*out);
        }
    }
    return chunks;
}

std::vector<Chunk> process_stream_payload(const nlohmann::json& payload,
                                          StreamState& state,
                                          StreamConversation& conv) {
    std::vector<Chunk> chunks;
    if (!payload.is_object()) return chunks;

    record_message_id(state, conv, payload.value("response_message_id", nlohmann::json(nullptr)));

    const auto operation = payload.value("o", nlohmann::json(nullptr));
    const std::string op = operation.is_string() ? operation.get<std::string>() : "";
    const auto value = payload.value("v", nlohmann::json(nullptr));

    if (op == "BATCH" && value.is_array()) {
        for (const auto& item : value) {
            auto sub = process_stream_payload(item, state, conv);
            chunks.insert(chunks.end(), sub.begin(), sub.end());
        }
        return chunks;
    }

    if (value.is_object() && value.value("response", nlohmann::json(nullptr)).is_object()) {
        const auto& response_obj = value["response"];
        record_message_id(state, conv, response_obj.value("message_id", nlohmann::json(nullptr)));
        if (response_obj.contains("status") && !response_obj["status"].is_null())
            record_stream_status(state, "response/status", response_obj["status"]);
        if (response_obj.value("fragments", nlohmann::json(nullptr)).is_array())
            for (auto&& c : process_fragments(response_obj["fragments"], state, true))
                chunks.push_back(c);
        return chunks;
    }

    const auto path = payload.value("p", nlohmann::json(nullptr));
    const std::string path_str = path.is_string() ? path.get<std::string>() : "";

    if (path_str == "response/fragments" && (op == "SET" || op == "APPEND") && value.is_array()) {
        for (auto&& c : process_fragments(value, state, op == "SET"))
            chunks.push_back(c);
        return chunks;
    }

    std::optional<std::string> frag_index =
        path.is_string() ? fragment_index_from_path(path_str) : std::optional<std::string>{};
    size_t slashes = 0;
    for (char c : path_str) slashes += (c == '/');

    if (frag_index.has_value() && slashes == 2 && (op == "SET" || op == "APPEND") &&
        value.is_object()) {
        auto kind = fragment_kind(value);
        record_fragment_kind(state, *frag_index, kind, op == "APPEND");
        const auto content = value.value("content", nlohmann::json(nullptr));
        if (kind.has_value() && content.is_string()) {
            std::string delta = op == "SET"
                ? state.snapshot_delta(*kind, content.get<std::string>())
                : state.append(*kind, content.get<std::string>());
            if (auto out = stream_output(*kind, delta)) chunks.push_back(*out);
        }
        return chunks;
    }

    if (path.is_string() && payload.contains("v")) {
        if (record_stream_status(state, path_str, value)) return chunks;
        if (frag_index.has_value() && path_str.size() >= 5 &&
            path_str.compare(path_str.size() - 5, 5, "/type") == 0 && value.is_string()) {
            nlohmann::json t;
            t["type"] = value;
            record_fragment_kind(state, *frag_index, fragment_kind(t));
            return chunks;
        }
        const std::string suffix = "/content";
        if (path_str.size() >= suffix.size() &&
            path_str.compare(path_str.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            value.is_string()) {
            std::optional<std::string> kind;
            auto it = frag_index.has_value() ? state.fragment_kinds.find(*frag_index)
                                             : state.fragment_kinds.end();
            kind = (it != state.fragment_kinds.end()) ? it->second : state.active_kind;
            if (!kind.has_value()) return chunks;
            std::string content = op == "SET"
                ? state.snapshot_delta(*kind, value.get<std::string>())
                : state.append(*kind, value.get<std::string>());
            if (auto out = stream_output(*kind, content)) chunks.push_back(*out);
        }
        return chunks;
    }

    if (value.is_string()) {
        if (!state.active_kind.has_value()) return chunks;
        if (auto out = stream_output(*state.active_kind,
                                     state.append(*state.active_kind, value.get<std::string>())))
            chunks.push_back(*out);
    }
    return chunks;
}

std::vector<Chunk> process_full_message(const nlohmann::json& biz_data,
                                        StreamState& state,
                                        StreamConversation& conv) {
    if (!biz_data.is_object())
        throw std::runtime_error("DeepSeek resume returned an invalid full message");
    nlohmann::json response = nullptr;
    if (biz_data.value("response", nlohmann::json(nullptr)).is_object()) {
        response = biz_data["response"];
    } else {
        auto rm = biz_data.value("response_message", nlohmann::json(nullptr));
        auto mm = biz_data.value("message", nlohmann::json(nullptr));
        if (rm.is_object()) response = rm;
        else if (mm.is_object()) response = mm;
        else if (biz_data.contains("fragments")) response = biz_data;
    }
    if (!response.is_object())
        throw std::runtime_error("DeepSeek resume returned an invalid full message");
    return process_stream_payload({{"v", {{"response", response}}}}, state, conv);
}

} // namespace g4f::deepseek
