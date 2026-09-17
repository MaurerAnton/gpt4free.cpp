// Port of g4f/models.py registry.
#include "g4f/models.hpp"
#include "g4f/errors.hpp"

namespace g4f {

const std::vector<Model>& all_models() {
    static const std::vector<Model> models = {
        DEEPSEEK_V3,
        DEEPSEEK_R1,
        DEEPSEEK_R1_DISTILL_LLAMA_70B,
        DEEPSEEK_R1_DISTILL_QWEN_1_5B,
        DEEPSEEK_R1_DISTILL_QWEN_14B,
    };
    return models;
}

const Model& find_model(const std::string& name) {
    for (const auto& m : all_models()) {
        if (m.name == name) return m;
    }
    throw ModelNotFoundError("model not found: " + name);
}

} // namespace g4f
