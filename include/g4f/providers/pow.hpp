// Port of the PoW protocol layer in g4f/Provider/needs_auth/DeepSeek.py
// (create_pow_response validation) and needs_auth/deepseek/pow.py
// (DeepSeekPOW.solve_challenge answer codec).
// The WASM grind itself is behind IPowSolver (native runtime: next commit).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace g4f::deepseek {

// Port of the challenge dict fields used by DeepSeekPOW.solve_challenge.
struct PowChallenge {
    std::string algorithm;
    std::string challenge;
    std::string salt;
    double difficulty = 0;
    long long expire_at = 0;
    std::string signature;
    std::string target_path;
};

// Port of the target_path/algorithm guards in create_pow_response.
// Throws std::runtime_error with the upstream messages.
void validate_challenge(const PowChallenge& challenge, const std::string& target_path);

// Port of prefix = f"{salt}_{expire_at}_" in DeepSeekHash.calculate_hash.
inline std::string pow_prefix(const std::string& salt, long long expire_at) {
    return salt + "_" + std::to_string(expire_at) + "_";
}

// Solver backend interface. Upstream runs pow_solver.wasm via wasmtime;
// native backends implement grind() the same way.
class IPowSolver {
public:
    virtual ~IPowSolver() = default;
    // Returns the integer answer, or std::nullopt when the solver gives up
    // (port: calculate_hash returning None -> RuntimeError upstream).
    virtual std::optional<long long> grind(const std::string& challenge,
                                           const std::string& prefix,
                                           double difficulty) = 0;
};

// Port of the ImportError path when wasmtime/numpy are missing:
// constructing or using it raises MissingRequirementsError.
class UnavailableSolver : public IPowSolver {
public:
    std::optional<long long> grind(const std::string&, const std::string&, double) override;
};

// Port of DeepSeekPOW.solve_challenge: validate -> grind -> base64(json).
// Throws MissingRequirementsError (no backend), std::runtime_error (no answer,
// both mirroring upstream).
std::string solve_challenge(const PowChallenge& challenge,
                            const std::string& target_path,
                            IPowSolver& solver);

// base64 encode (no external dep).
std::string base64_encode(const std::string& input);

} // namespace g4f::deepseek
