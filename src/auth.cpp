// Port of HAR loading in g4f/cookies.py.
#include "g4f/auth.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace g4f {
namespace {

// Port of _get_domain: host/:authority header matched against COOKIE_DOMAINS.
std::string match_domain(const nlohmann::json& entry) {
    const auto req = entry.value("request", nlohmann::json(nullptr));
    if (!req.is_object()) return "";
    std::string host;
    for (const auto& h : req.value("headers", nlohmann::json::array())) {
        std::string name = h.value("name", "");
        for (auto& c : name) c = (char)std::tolower((unsigned char)c);
        if (name == "host" || name == ":authority") host = h.value("value", "");
    }
    if (host.empty()) return "";
    for (const auto& d : COOKIE_DOMAINS)
        if (host.find(d) != std::string::npos) return d;
    return "";
}

// Port of _get_headers: lowercase names, minus content-length/cookie/":"*.
Headers entry_headers(const nlohmann::json& entry) {
    Headers out;
    const auto req = entry.value("request", nlohmann::json(nullptr));
    if (!req.is_object()) return out;
    for (const auto& h : req.value("headers", nlohmann::json::array())) {
        std::string name = h.value("name", "");
        for (auto& c : name) c = (char)std::tolower((unsigned char)c);
        if (name == "content-length" || name == "cookie" || (!name.empty() && name[0] == ':'))
            continue;
        out[name] = h.value("value", "");
    }
    return out;
}

void merge_file(const std::string& path, HarAuth& auth) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return; // port: FileNotFoundError -> pass
    nlohmann::json har;
    try {
        har = nlohmann::json::parse(f);
    } catch (...) {
        return; // port: JSONDecodeError -> pass
    }
    for (const auto& entry : har.value("log", nlohmann::json::object())
                                 .value("entries", nlohmann::json::array())) {
        if (match_domain(entry).empty()) continue;
        Headers hdrs = entry_headers(entry);
        for (const auto& [k, v] : hdrs) auth.headers[k] = v;
        const auto req = entry.value("request", nlohmann::json(nullptr));
        if (req.is_object()) {
            for (const auto& c : req.value("cookies", nlohmann::json::array())) {
                std::string name = c.value("name", "");
                if (!name.empty()) auth.cookies[name] = c.value("value", "");
            }
        }
    }
}

} // namespace

HarAuth parse_har_file(const std::string& path) {
    HarAuth auth;
    merge_file(path, auth);
    return auth;
}

HarAuth read_har_dir(const std::string& dir_path) {
    HarAuth auth;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec)) return auth; // port: not readable -> return
    for (const auto& de : fs::directory_iterator(dir_path, ec)) {
        if (ec) break;
        if (de.is_regular_file() && de.path().extension() == ".har")
            merge_file(de.path().string(), auth);
    }
    return auth;
}

} // namespace g4f
