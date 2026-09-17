// Port of g4f/Provider/needs_auth/DeepSeek.py — offline contract layer:
// endpoints, header defaults, thinking-mode selection, completion payload.
// Live PoW (WASM) and session I/O are later commits.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "g4f/providers/provider.hpp"
#include "g4f/typing.hpp"

namespace g4f::deepseek {

inline const std::string URL = "https://chat.deepseek.com";
inline const std::string DOMAIN = "chat.deepseek.com";
inline const std::string CHAT_SESSION_CREATE_ENDPOINT =
    URL + "/api/v0/chat_session/create";
inline const std::string CHAT_SESSION_CONTINUE_ENDPOINT =
    URL + "/api/v0/chat/continue";
inline const std::string CHAT_SESSION_RESUME_STREAM_ENDPOINT =
    URL + "/api/v0/chat/resume_stream";
inline const std::string CHAT_SESSION_DELETE_ENDPOINT =
    URL + "/api/v0/chat_session/delete";
inline const std::string CHAT_COMPLETION_ENDPOINT =
    URL + "/api/v0/chat/completion";
inline const std::string POW_CHALLENGE_ENDPOINT =
    URL + "/api/v0/chat/create_pow_challenge";
inline const std::string FILE_UPLOAD_ENDPOINT = URL + "/api/v0/file/upload_file";

inline const std::string POW_ALGORITHM = "DeepSeekHashV1";

// Port of CHAT_HEADER_DEFAULTS.
Headers default_chat_headers();

// Port of the provider class flags.
inline ProviderInfo provider_info(bool pow_available) {
    ProviderInfo info;
    info.label = "DeepSeek (HAR Auth)";
    info.url = URL;
    info.working = pow_available; // port: working = has_wasmtime_and_numpy
    info.needs_auth = true;
    info.supports_stream = true;
    info.supports_file_upload = true;
    return info;
}

inline const std::string DEFAULT_MODEL = "deepseek-v3";

// Port of the thinking-mode selection in create_async_generator:
// reasoning_effort != none  -> thinking on; else "deepseek-r1" in model.
bool select_thinking_enabled(const std::string& model,
                             const std::optional<std::string>& reasoning_effort);

// Port of _build_completion_payload().
nlohmann::json build_completion_payload(
    const std::string& chat_session_id,
    const std::optional<std::string>& parent_message_id,
    const std::string& model_type, // "default" | "expert" | "vision"
    const std::string& prompt,
    const std::vector<std::string>& ref_file_ids,
    bool thinking_enabled,
    bool search_enabled);

} // namespace g4f::deepseek
