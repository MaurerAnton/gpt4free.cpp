// Port of g4f/version.py + g4f/config.py (constants only).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <string>

namespace g4f {

inline const std::string PACKAGE_NAME = "g4f";
inline const std::string GITHUB_REPOSITORY = "xtekky/gpt4free";
// Port version of this C++ translation (not upstream release).
inline const std::string PORT_VERSION = "0.1.0";

// Port of REQUEST_TIMEOUT in g4f/version.py (seconds).
inline constexpr long REQUEST_TIMEOUT_S = 5;

} // namespace g4f
