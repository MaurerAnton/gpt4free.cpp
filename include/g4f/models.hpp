// Port of g4f/models.py (DeepSeek section) and
// g4f/providers/base_provider.py (capability flags subset).
// Upstream: https://github.com/xtekky/gpt4free (GPL-3.0)
#pragma once

#include <string>
#include <vector>

namespace g4f {

// Port of g4f.Model (fields used by this port).
struct Model {
    std::string name;
    std::string base_provider;
    std::vector<std::string> best_providers;
};

// Port of the `### "DeepSeek" ###` section in g4f/models.py.
inline const Model DEEPSEEK_V3{
    "deepseek-v3", "DeepSeek", {"Together"}};
inline const Model DEEPSEEK_R1{
    "deepseek-r1", "DeepSeek", {"Pollinations", "Together"}};
inline const Model DEEPSEEK_R1_DISTILL_LLAMA_70B{
    "deepseek-r1-distill-llama-70b", "DeepSeek", {"Together"}};
inline const Model DEEPSEEK_R1_DISTILL_QWEN_1_5B{
    "deepseek-r1-distill-qwen-1.5b", "DeepSeek", {"Together"}};
inline const Model DEEPSEEK_R1_DISTILL_QWEN_14B{
    "deepseek-r1-distill-qwen-14b", "DeepSeek", {"Together"}};

const std::vector<Model>& all_models();

// Lookup by name, throws ModelNotFoundError. Declared here,
// defined in src/models.cpp.
const Model& find_model(const std::string& name);

} // namespace g4f
