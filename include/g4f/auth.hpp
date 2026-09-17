// Port of the HAR cookie/header loading in g4f/cookies.py
// (_parse_har_file, _get_domain, _get_headers, read_cookie_files subset).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <map>
#include <string>
#include <vector>

#include "g4f/typing.hpp"

namespace g4f {

// Domains recognized for cookie attribution (port: COOKIE_DOMAINS use;
// DeepSeek-first subset, extend as providers are ported).
inline const std::vector<std::string> COOKIE_DOMAINS = {"deepseek.com"};

struct HarAuth {
    Cookies cookies; // request cookies for the matched domain
    Headers headers; // request headers (no content-length/cookie/:-prefixed)
};

// Port of _parse_har_file for a single file: last matching entry wins per
// field, mirroring the upstream per-entry dict merge.
HarAuth parse_har_file(const std::string& path);

// Port of read_cookie_files (top level only, no recursion): merges *.har in
// dir_path. Missing/unreadable dir or files yield empty auth (upstream logs
// and continues).
HarAuth read_har_dir(const std::string& dir_path);

} // namespace g4f
