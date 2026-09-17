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
#include "g4f/providers/envelope.hpp"
#include "g4f/providers/pow.hpp"
#include "g4f/providers/pow_wasm3.hpp"
#include "g4f/providers/sse.hpp"
#include "g4f/providers/stream.hpp"
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

void test_envelope() {
    using namespace g4f::deepseek;
    using nlohmann::json;
    // ok shape
    auto ok = unwrap_biz_response(
        json{{"code", 0}, {"msg", "ok"},
             {"data", {{"biz_code", 0}, {"biz_msg", "ok"}, {"biz_data", {{"id", "s1"}}}}}}, "ctx");
    CHECK(ok.biz_data["id"] == "s1");
    CHECK(extract_chat_session_id(ok.biz_data).value_or("") == "s1");
    // current shape
    auto cur = unwrap_biz_response(
        json::parse(R"({"data":{"biz_data":{"chat_session":{"id":"abc"}}}})"), "ctx");
    CHECK(extract_chat_session_id(cur.biz_data).value_or("") == "abc");
    CHECK(!extract_chat_session_id(json(nullptr)).has_value());
    // error shape
    bool caught = false;
    try {
        unwrap_biz_response(json{{"code", 40002}, {"msg", "Missing Token"}}, "PoW challenge");
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) ==
                 "DeepSeek PoW challenge failed (40002): Missing Token";
    }
    CHECK(caught);
    // allowed code 22 (resume path upstream)
    auto r22 = unwrap_biz_response(json{{"code", 22}, {"msg", "x"}, {"data", nullptr}},
                                   "resume", {"22"});
    CHECK(r22.message == "x");
    // invalid payload
    caught = false;
    try { unwrap_biz_response(json(42), "ctx"); }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught);
}

void test_sse_frames() {
    using namespace g4f::deepseek;
    SseFrameParser p;
    std::vector<SseFrame> got;
    auto feed = [&](const std::string& l) {
        auto v = p.feed_line(l);
        got.insert(got.end(), v.begin(), v.end());
    };
    feed(": heartbeat");
    feed("event: patch");
    feed("data: {\"o\":\"APPEND\",\"v\":1}");
    feed("");
    CHECK(got.size() == 1);
    CHECK(got[0].first == "patch");
    CHECK(got[0].second["o"] == "APPEND");
    feed("data: [DONE]");
    feed("");
    CHECK(got.size() == 1); // [DONE] yields nothing
    bool threw = false;
    try {
        feed("data: {oops");
        feed("");
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    // default event type + flush
    SseFrameParser q;
    q.feed_line("data: {\"a\":true}");
    auto tail = q.flush();
    CHECK(tail.size() == 1 && tail[0].first == "message");
}

void test_stream_state() {
    using namespace g4f::deepseek;
    using nlohmann::json;
    CHECK(is_message_status("FINISHED") && !is_message_status("WIPX"));
    CHECK(finish_reason_for("FINISHED") == "stop");
    CHECK(finish_reason_for("CONTEXT_LENGTH_EXCEEDED") == "length");
    CHECK(finish_reason_for("?") == "");
    CHECK(fragment_kind(json{{"type", "THINK"}}).value_or("") == "reasoning");
    CHECK(fragment_kind(json{{"type", "RESPONSE"}}).value_or("") == "response");
    CHECK(!fragment_kind(json{{"type", "TIP"}}).has_value());
    CHECK(fragment_kind(json::object()).value_or("") == "response");

    // Snapshot with think + response fragments.
    StreamState st;
    StreamConversation conv;
    auto chunks = process_stream_payload(
        json::parse(R"({"v":{"response":{"message_id":"m1","status":"WIP","fragments":[{"type":"THINK","content":"hmm"},{"type":"RESPONSE","content":"hello"}]}}})"),
        st, conv);
    CHECK(chunks.size() == 2);
    CHECK(chunks[0].reasoning && chunks[0].text == "hmm");
    CHECK(!chunks[1].reasoning && chunks[1].text == "hello");
    CHECK(conv.parent_message_id == "m1");
    CHECK(st.status == "WIP");

    // APPEND patch on the same response fragment emits only the delta.
    auto more = process_stream_payload(
        json::parse(R"({"p":"response/fragments/1/content","o":"APPEND","v":" world"})"),
        st, conv);
    CHECK(more.size() == 1 && more[0].text == " world");

    // SET patch resends full content: only new tail emitted (dedup).
    auto again = process_stream_payload(
        json::parse(R"({"p":"response/fragments/1/content","o":"SET","v":"hello world"})"),
        st, conv);
    CHECK(again.empty());

    // Status patch + BATCH.
    StreamState st2;
    StreamConversation c2;
    auto fin = process_stream_payload(json::parse(R"({"p":"response/status","o":"SET","v":"FINISHED"})"),
                                      st2, c2);
    CHECK(fin.empty() && st2.status == "FINISHED");
    auto batch = process_stream_payload(
        json::parse(R"({"o":"BATCH","v":[{"p":"response/status","o":"SET","v":"WIP"}]})"),
        st2, c2);
    CHECK(batch.empty() && st2.status == "WIP");

    // Full-message (resume) path.
    StreamState st3;
    StreamConversation c3;
    auto full = process_full_message(
        json::parse(R"({"response":{"fragments":[{"type":"RESPONSE","content":"done"}]}})"),
        st3, c3);
    CHECK(full.size() == 1 && full[0].text == "done");
    bool threw = false;
    try { process_full_message(json::parse(R"({"nope":1})"), st3, c3); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}

int main() {
    test_errors();
    test_typing();
    test_models();
    test_deepseek_contract();
    test_sse();
    test_pow_protocol();
    test_pow_runtime();
    test_envelope();
    test_sse_frames();
    test_stream_state();
    if (failures == 0) std::cout << "all tests passed\n";
    return failures == 0 ? 0 : 1;
}
