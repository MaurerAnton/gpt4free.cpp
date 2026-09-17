// Port of g4f/providers/base_provider.py (capability-flag subset).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <string>

namespace g4f {

struct ProviderInfo {
    std::string label;
    std::string url;
    bool working = false;
    bool needs_auth = false;
    bool supports_stream = false;
    bool supports_file_upload = false;
};

} // namespace g4f
