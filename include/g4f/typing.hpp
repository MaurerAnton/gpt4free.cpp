// Port of g4f/typing.py (Messages/Cookies subset; no PIL on C++).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <map>
#include <string>
#include <vector>

namespace g4f {

using Cookies = std::map<std::string, std::string>;
using Headers = std::map<std::string, std::string>;

struct Message {
    std::string role;    // "system" | "user" | "assistant" | ...
    std::string content;
};

using Messages = std::vector<Message>;

// Port of g4f.providers.helper.get_last_user_message
inline std::string get_last_user_message(const Messages& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "user") return it->content;
    }
    return messages.empty() ? "" : messages.back().content;
}

} // namespace g4f
