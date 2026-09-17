// Port of the DeepSeek PoW protocol layer.
#include "g4f/providers/pow.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "g4f/errors.hpp"
#include "g4f/providers/deepseek.hpp"

namespace g4f::deepseek {

void validate_challenge(const PowChallenge& challenge, const std::string& target_path) {
    if (challenge.target_path != target_path) {
        throw std::runtime_error(
            "DeepSeek returned a PoW challenge for an unexpected target path: '" +
            challenge.target_path + "'");
    }
    if (challenge.algorithm != POW_ALGORITHM) {
        throw std::runtime_error(
            "DeepSeek returned an unsupported PoW algorithm: '" +
            challenge.algorithm + "'");
    }
}

std::optional<long long> UnavailableSolver::grind(const std::string&,
                                                  const std::string&,
                                                  double) {
    // Port of: raise ImportError("wasmtime and numpy are required for PoW solving")
    throw MissingRequirementsError("wasmtime and numpy are required for PoW solving"
                                   " (no native PoW runtime linked)");
}

std::string solve_challenge(const PowChallenge& challenge,
                            const std::string& target_path,
                            IPowSolver& solver) {
    validate_challenge(challenge, target_path);
    auto answer = solver.grind(challenge.challenge,
                               pow_prefix(challenge.salt, challenge.expire_at),
                               challenge.difficulty);
    if (!answer.has_value()) {
        throw std::runtime_error("DeepSeek PoW solver returned no answer");
    }
    nlohmann::json result;
    result["algorithm"] = challenge.algorithm;
    result["challenge"] = challenge.challenge;
    result["salt"] = challenge.salt;
    result["answer"] = *answer;
    result["signature"] = challenge.signature;
    result["target_path"] = challenge.target_path;
    return base64_encode(result.dump());
}

std::string base64_encode(const std::string& input) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        unsigned n = (unsigned char)input[i] << 16;
        size_t len = 1;
        if (i + 1 < input.size()) { n |= (unsigned char)input[i + 1] << 8; ++len; }
        if (i + 2 < input.size()) { n |= (unsigned char)input[i + 2]; ++len; }
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(len > 1 ? table[(n >> 6) & 63] : '=');
        out.push_back(len > 2 ? table[n & 63] : '=');
    }
    return out;
}

} // namespace g4f::deepseek
