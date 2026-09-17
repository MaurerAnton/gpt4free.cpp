// Port of the DeepSeek business envelope (_unwrap_biz_response,
// _extract_chat_session_id in Provider/needs_auth/DeepSeek.py).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace g4f::deepseek {

struct BizResponse {
    nlohmann::json code;    // may be null/int/string
    nlohmann::json biz_data; // may be null
    std::string message;
};

// Port of _unwrap_biz_response: validates the {code,data:{biz_code,
// biz_msg,biz_data}} envelope. Throws std::runtime_error (upstream messages).
// allowed_codes ports the allowed_codes kwarg (e.g. {"22"} for resume).
BizResponse unwrap_biz_response(const nlohmann::json& payload,
                                const std::string& context,
                                const std::vector<std::string>& allowed_codes = {});

// Port of _extract_chat_session_id: reads current (chat_session.id) and
// legacy (biz_data.id) shapes. Returns nullopt when absent.
std::optional<std::string> extract_chat_session_id(const nlohmann::json& biz_data);

} // namespace g4f::deepseek
