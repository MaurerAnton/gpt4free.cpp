// Port of the DeepSeek envelope helpers.
#include "g4f/providers/envelope.hpp"

namespace g4f::deepseek {

namespace {
bool code_ok(const nlohmann::json& code,
             const std::vector<std::string>& allowed) {
    if (code.is_null()) return true;
    if (code.is_number() && code.get<long long>() == 0) return true;
    if (code.is_string() && code.get<std::string>() == "0") return true;
    std::string s = code.is_string() ? code.get<std::string>()
                                     : code.dump();
    for (const auto& a : allowed)
        if (s == a) return true;
    return false;
}
} // namespace

BizResponse unwrap_biz_response(const nlohmann::json& payload,
                                const std::string& context,
                                const std::vector<std::string>& allowed_codes) {
    if (!payload.is_object()) {
        throw std::runtime_error("DeepSeek " + context +
                                 " returned an invalid JSON response");
    }
    BizResponse out;
    out.biz_data = nullptr;
    const auto data = payload.value("data", nlohmann::json(nullptr));
    if (data.is_object()) {
        nlohmann::json biz_code = data.value("biz_code", payload.value("code", nlohmann::json(nullptr)));
        out.code = biz_code;
        std::string biz_msg = data.value("biz_msg", "");
        out.message = !biz_msg.empty() ? biz_msg : payload.value("msg", "");
        out.biz_data = data.value("biz_data", nlohmann::json(nullptr));
    } else {
        out.code = payload.value("code", nlohmann::json(nullptr));
        out.message = payload.value("msg", "");
    }
    // allowed_codes ports the allowed_codes kwarg (code 22 is whitelisted
    // by resume callers upstream).
    if (!code_ok(out.code, allowed_codes)) {
        std::string code_str =
            out.code.is_string() ? out.code.get<std::string>() : out.code.dump();
        std::string detail = out.message.empty() ? "unknown business error" : out.message;
        throw std::runtime_error("DeepSeek " + context + " failed (" + code_str +
                                 "): " + detail);
    }
    return out;
}

std::optional<std::string> extract_chat_session_id(const nlohmann::json& biz_data) {
    if (!biz_data.is_object()) return std::nullopt;
    const auto session = biz_data.value("chat_session", nlohmann::json(nullptr));
    if (session.is_object()) {
        const auto id = session.value("id", nlohmann::json(nullptr));
        if (id.is_string() && !id.get<std::string>().empty())
            return id.get<std::string>();
    }
    const auto id = biz_data.value("id", nlohmann::json(nullptr));
    if (id.is_string() && !id.get<std::string>().empty())
        return id.get<std::string>();
    // Numeric ids: stringify (upstream returns them as-is; string form here).
    if (id.is_number()) return id.dump();
    return std::nullopt;
}

} // namespace g4f::deepseek
