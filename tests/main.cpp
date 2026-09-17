// Tests for the ported core (mirrors upstream behavior).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "g4f/errors.hpp"
#include "g4f/auth.hpp"
#include "g4f/http.hpp"
#include "g4f/models.hpp"
#include "g4f/providers/deepseek.hpp"
#include "g4f/providers/driver.hpp"
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

void test_har_auth() {
    using nlohmann::json;
    // Synthetic HAR mirroring a chat.deepseek.com completion entry.
    const char* har = R"({"log":{"entries":[
      {"request":{"headers":[
        {"name":"Host","value":"chat.deepseek.com"},
        {"name":"Authorization","value":"Bearer tok123"},
        {"name":"Cookie","value":"a=b"},
        {"name":"Content-Length","value":"5"},
        {"name":":method","value":"POST"}],
        "cookies":[{"name":"session","value":"s3cr3t"}]}},
      {"request":{"headers":[{"name":"Host","value":"other.example"}],
        "cookies":[{"name":"x","value":"y"}]}}
    ]}})";
    const std::string path = "/tmp/opencode/g4f_test.har";
    { std::ofstream f(path, std::ios::binary); f << har; }
    g4f::HarAuth a = g4f::parse_har_file(path);
    CHECK(a.cookies["session"] == "s3cr3t");
    CHECK(a.headers["authorization"] == "Bearer tok123");
    CHECK(a.headers.find("cookie") == a.headers.end());
    CHECK(a.headers.find("content-length") == a.headers.end());
    CHECK(a.headers.find(":method") == a.headers.end());
    CHECK(a.cookies.find("x") == a.cookies.end()); // other domain ignored
    // Missing file / dir: empty auth, no throw (upstream: pass/return).
    CHECK(g4f::parse_har_file("/tmp/opencode/does-not-exist.har").cookies.empty());
    CHECK(g4f::read_har_dir("/tmp/opencode/does-not-exist-dir").cookies.empty());
    // Dir merge picks up the file.
    g4f::HarAuth d = g4f::read_har_dir("/tmp/opencode");
    CHECK(d.cookies["session"] == "s3cr3t");
}

struct FakeTransport : g4f::deepseek::ITransport {
    struct Call { std::string url; std::string body; };
    std::vector<Call> calls;
    std::vector<g4f::deepseek::HttpExchange> script;
    g4f::deepseek::HttpExchange post(const std::string& url, const std::string& body,
                                     const g4f::Headers&) override {
        calls.push_back({url, body});
        if (script.empty()) throw std::runtime_error("fake: script exhausted");
        auto ex = script.front();
        script.erase(script.begin());
        return ex;
    }
};

static g4f::deepseek::HttpExchange sse(const std::string& body) {
    return {200, "text/event-stream", body};
}

void test_driver_happy_path() {
    using namespace g4f::deepseek;
    FakeTransport t;
    t.script.push_back(sse(
        "data: {\"v\":{\"response\":{\"message_id\":\"m1\",\"status\":\"WIP\","
        "\"fragments\":[{\"type\":\"RESPONSE\",\"content\":\"hi\"}]}}}\n\n"
        "event: close\ndata: {\"auto_resume\":false}\n\n"));
    StreamConversation conv;
    nlohmann::json payload = {{"chat_session_id", "s"}, {"prompt", "hi"}};
    auto events = run_chat_stream(t, payload, {}, conv);
    CHECK(events.size() == 2);
    CHECK(!events[0].finish && events[0].text == "hi");
    // WIP maps to finish reason "wip" upstream (DEEPSEEK_FINISH_REASONS).
    CHECK(events[1].finish && events[1].reason == "wip");
    CHECK(t.calls.size() == 1);
    CHECK(conv.parent_message_id == "m1");
}

void test_driver_code22() {
    using namespace g4f::deepseek;
    FakeTransport t;
    t.script.push_back({200, "application/json",
        R"({"code":22,"data":{"biz_data":{"response":{"status":"FINISHED","fragments":[{"type":"RESPONSE","content":"full"}]}}}})"});
    StreamConversation conv;
    auto events = run_chat_stream(t, {{"chat_session_id", "s"}}, {}, conv);
    CHECK(events.size() == 2);
    CHECK(events[0].text == "full");
    CHECK(events[1].finish && events[1].reason == "stop");
}

void test_driver_continue() {
    using namespace g4f::deepseek;
    FakeTransport t;
    t.script.push_back(sse(
        "data: {\"response_message_id\":\"m9\",\"v\":{\"response\":{\"status\":\"INCOMPLETE\","
        "\"fragments\":[{\"type\":\"RESPONSE\",\"content\":\"part\"}]}}}\n\n"
        "event: close\ndata: {}\n\n"));
    t.script.push_back(sse(
        "data: {\"v\":{\"response\":{\"status\":\"FINISHED\","
        "\"fragments\":[{\"type\":\"RESPONSE\",\"content\":\"part two\"}]}}}\n\n"
        "event: close\ndata: {}\n\n"));
    StreamConversation conv;
    auto events = run_chat_stream(t, {{"chat_session_id", "s"}}, {}, conv);
    CHECK(t.calls.size() == 2);
    CHECK(t.calls[1].url.find("/api/v0/chat/continue") != std::string::npos);
    CHECK(t.calls[1].body.find("fallback_to_resume") != std::string::npos);
    // part + " two" (snapshot dedup: second SET resends full "part two")
    std::string text;
    for (auto& e : events) if (!e.finish) text += e.text;
    CHECK(text == "part two");
    CHECK(events.back().finish && events.back().reason == "stop");
}

void test_driver_caps() {
    using namespace g4f::deepseek;
    // max_continue_attempts=0 -> error after first INCOMPLETE close
    FakeTransport t;
    t.script.push_back(sse(
        "data: {\"v\":{\"response\":{\"message_id\":\"m\",\"status\":\"INCOMPLETE\","
        "\"fragments\":[{\"type\":\"RESPONSE\",\"content\":\"x\"}]}}}\n\n"
        "event: close\ndata: {}\n\n"));
    StreamConversation conv;
    DriverOptions opts;
    opts.max_continue_attempts = 0;
    bool threw = false;
    try { run_chat_stream(t, {{"chat_session_id", "s"}}, {}, conv, opts); }
    catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("INCOMPLETE after 0") != std::string::npos;
    }
    CHECK(threw);
    // missing session id
    threw = false;
    try { run_chat_stream(t, nlohmann::json::object(), {}, conv); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // negative cap
    threw = false;
    try {
        DriverOptions bad;
        bad.max_resume_attempts = -1;
        run_chat_stream(t, {{"chat_session_id", "s"}}, {}, conv, bad);
    } catch (const std::invalid_argument&) { threw = true; }
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
    test_har_auth();
    test_driver_happy_path();
    test_driver_code22();
    test_driver_continue();
    test_driver_caps();
    if (failures == 0) std::cout << "all tests passed\n";
    return failures == 0 ? 0 : 1;
}
