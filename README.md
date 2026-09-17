# gpt4free.cpp

C++17 port of [xtekky/gpt4free](https://github.com/xtekky/gpt4free)
(python, ~75k lines / 302 files at time of port).
**License: GPL-3.0** — this is a derivative work, see `LICENSE`.
Upstream `LICENSE` copied verbatim; ported files carry the same license.

## Layout (mirrors `g4f/`)

| C++ | Python upstream |
|---|---|
| `include/g4f/version.hpp` | `g4f/version.py` + `g4f/config.py` |
| `include/g4f/errors.hpp` | `g4f/errors.py` |
| `include/g4f/typing.hpp` | `g4f/typing.py` (Messages/Cookies) |
| `include/g4f/models.hpp` + `src/models.cpp` | `g4f/models.py` (ported: DeepSeek section first) |
| `include/g4f/http.hpp` + `src/http.cpp` | `g4f/requests/__init__.py` (`StreamSession` subset, libcurl) |
| `include/g4f/providers/provider.hpp` | `g4f/providers/base_provider.py` |
| `include/g4f/providers/deepseek.hpp` + `src/providers/deepseek.cpp` | `g4f/Provider/needs_auth/DeepSeek.py` (payload/contract layer; PoW WASM + live I/O later) |
| `tests/` | upstream `etc/unittest` (subset) |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/g4f_tests
```

Deps: libcurl, nlohmann/json (FetchContent if not installed).

## Port progress

- [x] scaffold, version, errors, typing
- [x] model registry (DeepSeek section)
- [x] HTTP client + SSE reader
- [x] DeepSeek payload builders (offline-testable)
- [ ] DeepSeek PoW (`DeepSeekHashV1` WASM → native)
- [ ] DeepSeek live session/completion I/O
- [ ] remaining providers/models (on demand)
