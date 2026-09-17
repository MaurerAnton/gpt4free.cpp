// Native PoW runtime: runs the vendored pow_solver.wasm via wasm3,
// mirroring needs_auth/deepseek/pow.py (DeepSeekHash) call for call.
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "g4f/providers/pow.hpp"

namespace g4f::deepseek {

// Loads thirdparty/pow_solver.wasm (or given bytes) and exposes the exact
// call sequence from DeepSeekHash.calculate_hash:
//   retptr = stack(-16); malloc+write challenge/prefix;
//   wasm_solve(retptr, cptr, clen, pptr, plen, double(difficulty));
//   status = i32[retptr]; answer = int(f64[retptr+8]); stack(+16).
class Wasm3PowSolver : public IPowSolver {
public:
    explicit Wasm3PowSolver(const std::string& wasm_path);
    explicit Wasm3PowSolver(const std::vector<uint8_t>& wasm_bytes);
    ~Wasm3PowSolver();

    Wasm3PowSolver(const Wasm3PowSolver&) = delete;
    Wasm3PowSolver& operator=(const Wasm3PowSolver&) = delete;

    std::optional<long long> grind(const std::string& challenge,
                                   const std::string& prefix,
                                   double difficulty) override;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace g4f::deepseek
