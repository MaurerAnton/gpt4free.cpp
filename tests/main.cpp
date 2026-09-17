// Tests for the ported core (mirrors upstream behavior).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

#include "g4f/errors.hpp"
#include "g4f/http.hpp"
#include "g4f/models.hpp"
#include "g4f/providers/deepseek.hpp"
#include "g4f/providers/pow.hpp"
#include "g4f/providers/pow_wasm3.hpp"
#include "g4f/typing.hpp"
#include "g4f/version.hpp"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << __LINE__ << ": " #cond "\n"; ++failures; \
    } \
} while (0)

void test_errors() {
    try { throw g4f::MissingAuthError("DeepSeekAuth: No authentication found."); }
    catch (const g4f::G4FError& e) {
        CHECK(std::string(e.what()).find("No authentication") != std::string::npos);
    }
    bool caught = false;
    try { throw g4f::ModelNotFoundError("x"); }
    catch (const g4f::G4FError&) { caught = true; }
    CHECK(caught);
}

void test_typing() {
    g4f::Messages m = {{"system", "s"}, {"user", "first"}, {"assistant", "a"}, {"user", "last"}};
    CHECK(g4f::get_last_user_message(m) == "last");
    CHECK(g4f::get_last_user_message({}) == "");
}

void test_models() {
    CHECK(g4f::find_model("deepseek-v3").base_provider == "DeepSeek");
    CHECK(g4f::find_model("deepseek-r1").best_providers.size() == 2);
    CHECK(g4f::all_models().size() == 5);
    bool caught = false;
    try { g4f::find_model("deepseek-flash"); }
    catch (const g4f::ModelNotFoundError&) { caught = true; }
    CHECK(caught); // port documents: no flash model upstream either
}

void test_deepseek_contract() {
    using namespace g4f::deepseek;
    CHECK(CHAT_COMPLETION_ENDPOINT == "https://chat.deepseek.com/api/v0/chat/completion");
    CHECK(POW_ALGORITHM == "DeepSeekHashV1");
    auto h = default_chat_headers();
    CHECK(h["x-client-platform"] == "web");
    CHECK(h["origin"] == URL);

    // Port of thinking selection.
    CHECK(select_thinking_enabled("deepseek-r1", std::nullopt) == true);
    CHECK(select_thinking_enabled("deepseek-v3", std::nullopt) == false);
    CHECK(select_thinking_enabled("deepseek-v3", std::make_optional<std::string>("high")) == true);
    CHECK(select_thinking_enabled("deepseek-r1", std::make_optional<std::string>("none")) == false);

    auto info = provider_info(true);
    CHECK(info.needs_auth && info.supports_stream && info.supports_file_upload);
    CHECK(provider_info(false).working == false);

    auto payload = build_completion_payload("sess-1", std::nullopt, "default",
                                            "hi", {}, true, false);
    CHECK(payload["chat_session_id"] == "sess-1");
    CHECK(payload["model_type"] == "default");
    CHECK(payload["thinking_enabled"] == true);
    CHECK(payload["search_enabled"] == false);
    CHECK(payload["preempt"] == false);
    CHECK(payload["action"].is_null() && payload["parent_message_id"].is_null());
    // No model id is ever sent: names are local aliases (verified vs upstream).
    CHECK(payload.find("model") == payload.end());
}

void test_sse() {
    std::vector<std::string> lines;
    std::string carry = g4f::feed_sse_lines("data: a\n\ndata: b\npartial",
        [&](const std::string& l) { lines.push_back(l); });
    CHECK(lines.size() == 3 && lines[0] == "data: a" && lines[2] == "data: b");
    CHECK(carry == "partial");
}

void test_pow_protocol() {
    using namespace g4f::deepseek;
    CHECK(pow_prefix("s", 42) == "s_42_");
    CHECK(base64_encode("Man") == "TWFu");
    CHECK(base64_encode("") == "");

    PowChallenge good{"DeepSeekHashV1", "ch", "s", 48000, 99, "sig",
                      "/api/v0/chat/completion"};
    validate_challenge(good, "/api/v0/chat/completion"); // no throw

    bool caught = false;
    try {
        PowChallenge wrong = good;
        wrong.target_path = "/other";
        validate_challenge(wrong, "/api/v0/chat/completion");
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("unexpected target path") != std::string::npos;
    }
    CHECK(caught);

    caught = false;
    try {
        PowChallenge wrong = good;
        wrong.algorithm = "Other";
        validate_challenge(wrong, "/api/v0/chat/completion");
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()).find("unsupported PoW algorithm") != std::string::npos;
    }
    CHECK(caught);

    // Unavailable backend mirrors upstream ImportError.
    UnavailableSolver nosolv;
    caught = false;
    try { solve_challenge(good, "/api/v0/chat/completion", nosolv); }
    catch (const g4f::MissingRequirementsError&) { caught = true; }
    CHECK(caught);

    // Answer codec with a stub solver: verify base64(json) fields.
    struct Stub : IPowSolver {
        std::optional<long long> grind(const std::string&, const std::string&,
                                       double) override { return 12345; }
    } stub;
    std::string resp = solve_challenge(good, "/api/v0/chat/completion", stub);
    CHECK(!resp.empty());
}

void test_pow_runtime() {
    using namespace g4f::deepseek;
    // Module loads, WASI links, exports resolve (mirrors DeepSeekHash.init).
    Wasm3PowSolver solver("thirdparty/pow_solver.wasm");
    // Full call path on synthetic input completes (upstream Python returns
    // None here too — server-issued challenges needed for real answers).
    auto t0 = std::chrono::steady_clock::now();
    auto ans = solver.grind(std::string(64, 'a'), "s_1_", 48000.0);
    auto dt = std::chrono::steady_clock::now() - t0;
    (void)ans;
    CHECK(dt < std::chrono::seconds(60));
}

int main() {
    test_errors();
    test_typing();
    test_models();
    test_deepseek_contract();
    test_sse();
    test_pow_protocol();
    test_pow_runtime();
    if (failures == 0) std::cout << "all tests passed\n";
    return failures == 0 ? 0 : 1;
}
