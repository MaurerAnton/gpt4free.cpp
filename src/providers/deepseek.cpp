// Port of g4f/Provider/needs_auth/DeepSeek.py (contract layer).
#include "g4f/providers/deepseek.hpp"

namespace g4f::deepseek {

Headers default_chat_headers() {
    return {
        {"accept", "*/*"},
        {"cache-control", "no-cache"},
        {"content-type", "application/json"},
        {"origin", URL},
        {"referer", URL + "/a/chat/"},
        {"x-client-bundle-id", "com.deepseek.chat"},
        {"x-client-locale", "en_US"},
        {"x-client-platform", "web"},
        {"x-client-version", "2.4.0"},
    };
}

bool select_thinking_enabled(const std::string& model,
                             const std::optional<std::string>& reasoning_effort) {
    if (reasoning_effort.has_value()) {
        return *reasoning_effort != "none";
    }
    return !model.empty() && model.find("deepseek-r1") != std::string::npos;
}

nlohmann::json build_completion_payload(
    const std::string& chat_session_id,
    const std::optional<std::string>& parent_message_id,
    const std::string& model_type,
    const std::string& prompt,
    const std::vector<std::string>& ref_file_ids,
    bool thinking_enabled,
    bool search_enabled) {
    if (chat_session_id.empty()) {
        throw std::invalid_argument("chat_session_id is required for completion");
    }
    nlohmann::json j;
    j["action"] = nullptr;
    j["chat_session_id"] = chat_session_id;
    j["parent_message_id"] =
        parent_message_id.has_value() ? nlohmann::json(*parent_message_id) : nlohmann::json(nullptr);
    j["model_type"] = model_type;
    j["prompt"] = prompt;
    j["ref_file_ids"] = ref_file_ids;
    j["thinking_enabled"] = thinking_enabled;
    j["search_enabled"] = search_enabled;
    j["preempt"] = false;
    return j;
}

} // namespace g4f::deepseek
