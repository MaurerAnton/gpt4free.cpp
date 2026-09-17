// Native PoW runtime on wasm3. Mirrors DeepSeekHash (pow.py).
#include "g4f/providers/pow_wasm3.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

extern "C" {
#include "wasm3.h"
#include "m3_api_wasi.h"
}

namespace g4f::deepseek {
namespace {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("WASM file not found: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

void check(M3Result r, const char* what) {
    if (r != m3Err_none) throw std::runtime_error(std::string(what) + ": " + r);
}

uint64_t f64bits(double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return u;
}

} // namespace

struct Wasm3PowSolver::Impl {
    IM3Environment env = nullptr;
    IM3Runtime runtime = nullptr;
    IM3Module module = nullptr;
    IM3Function fn_malloc = nullptr; // __wbindgen_export_0
    IM3Function fn_stack = nullptr;  // __wbindgen_add_to_stack_pointer
    IM3Function fn_solve = nullptr;  // wasm_solve
};

Wasm3PowSolver::Wasm3PowSolver(const std::vector<uint8_t>& wasm_bytes)
    : impl_(new Impl) {
    M3Result r;
    impl_->env = m3_NewEnvironment();
    if (!impl_->env) throw std::runtime_error("m3_NewEnvironment failed");
    impl_->runtime = m3_NewRuntime(impl_->env, 256 * 1024, nullptr);
    if (!impl_->runtime) throw std::runtime_error("m3_NewRuntime failed");

    IM3Module module = nullptr;
    r = m3_ParseModule(impl_->env, &module, wasm_bytes.data(), wasm_bytes.size());
    check(r, "m3_ParseModule");
    r = m3_LoadModule(impl_->runtime, module);
    check(r, "m3_LoadModule");
    impl_->module = module;
    r = m3_LinkWASI(module); // port: linker.define_wasi()
    check(r, "m3_LinkWASI");

    r = m3_FindFunction(&impl_->fn_malloc, impl_->runtime, "__wbindgen_export_0");
    check(r, "find __wbindgen_export_0");
    r = m3_FindFunction(&impl_->fn_stack, impl_->runtime,
                        "__wbindgen_add_to_stack_pointer");
    check(r, "find __wbindgen_add_to_stack_pointer");
    r = m3_FindFunction(&impl_->fn_solve, impl_->runtime, "wasm_solve");
    check(r, "find wasm_solve");
}

Wasm3PowSolver::Wasm3PowSolver(const std::string& wasm_path)
    : Wasm3PowSolver(read_file(wasm_path)) {}

Wasm3PowSolver::~Wasm3PowSolver() {
    if (impl_) {
        if (impl_->runtime) m3_FreeRuntime(impl_->runtime);
        if (impl_->env) m3_FreeEnvironment(impl_->env);
        delete impl_;
    }
}

std::optional<long long> Wasm3PowSolver::grind(const std::string& challenge,
                                               const std::string& prefix,
                                               double difficulty) {
    auto malloc_str = [this](uint32_t len) {
        check(m3_CallV(impl_->fn_malloc, len, (uint32_t)1), "malloc");
        uint32_t ret = 0;
        check(m3_GetResultsV(impl_->fn_malloc, &ret), "malloc results");
        return ret;
    };
    auto stack_add = [this](int32_t delta) {
        check(m3_CallV(impl_->fn_stack, (uint32_t)delta), "stack");
        uint32_t ret = 0;
        check(m3_GetResultsV(impl_->fn_stack, &ret), "stack results");
        return ret;
    };

    size_t mem_size = 0;
    uint8_t* mem = m3_GetMemory(impl_->module, &mem_size, 0);
    if (!mem) throw std::runtime_error("no WASM memory");

    auto write_str = [&](const std::string& s) {
        uint32_t ptr = malloc_str((uint32_t)s.size());
        mem = m3_GetMemory(impl_->module, &mem_size, 0); // may grow
        if ((uint64_t)ptr + s.size() > mem_size)
            throw std::runtime_error("WASM memory write out of bounds");
        std::memcpy(mem + ptr, s.data(), s.size());
        return std::pair<uint32_t, uint32_t>(ptr, (uint32_t)s.size());
    };

    uint32_t retptr = stack_add(-16);
    try {
        auto [cptr, clen] = write_str(challenge);
        auto [pptr, plen] = write_str(prefix);
        check(m3_CallV(impl_->fn_solve, retptr, cptr, clen, pptr, plen,
                       difficulty),
              "wasm_solve");

        mem = m3_GetMemory(impl_->module, &mem_size, 0);
        if ((uint64_t)retptr + 16 > mem_size)
            throw std::runtime_error("WASM result read out of bounds");
        int32_t status = 0;
        std::memcpy(&status, mem + retptr, 4);
        if (status == 0) return std::nullopt; // port: calculate_hash -> None
        double value = 0;                     // port: numpy float64 -> int
        std::memcpy(&value, mem + retptr + 8, 8);
        return (long long)value;
    } catch (...) {
        stack_add(16); // port: finally stack(+16)
        throw;
    }
    stack_add(16);
    // unreachable (kept symmetric with upstream finally)
    return std::nullopt;
}

} // namespace g4f::deepseek
