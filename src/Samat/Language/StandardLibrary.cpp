#include "Samat/Language/StandardLibrary.hpp"
#include "RuntimeABI.h"
#include <limits>
#include <unordered_map>

namespace jm::script {
int runtimeStandardOperation(std::string_view name) {
    if (name.starts_with("builtin.math."))
        name.remove_prefix(13);
    const std::unordered_map<std::string, int> operations{{"trunc", JM_RT_TRUNC},
                                                          {"tan", JM_RT_TAN},
                                                          {"asin", JM_RT_ASIN},
                                                          {"acos", JM_RT_ACOS},
                                                          {"atan", JM_RT_ATAN},
                                                          {"atan2", JM_RT_ATAN2},
                                                          {"log", JM_RT_LOG},
                                                          {"log10", JM_RT_LOG10},
                                                          {"exp", JM_RT_EXP},
                                                          {"lerp", JM_RT_LERP},
                                                          {"degToRad", JM_RT_DEG_TO_RAD},
                                                          {"radToDeg", JM_RT_RAD_TO_DEG},
                                                          {"sign", JM_RT_SIGN},
                                                          {"fract", JM_RT_FRACT},
                                                          {"smoothstep", JM_RT_SMOOTHSTEP},
                                                          {"seed", JM_RT_SEED},
                                                          {"random", JM_RT_RANDOM},
                                                          {"randomInt", JM_RT_RANDOM_INT},
                                                          {"randomFloat", JM_RT_RANDOM_FLOAT},
                                                          {"timeNow", JM_RT_TIME_NOW},
                                                          {"timeElapsed", JM_RT_TIME_ELAPSED},
                                                          {"builtin.time.now", JM_RT_TIME_NOW},
                                                          {"builtin.time.elapsed", JM_RT_TIME_ELAPSED},
                                                          {"builtin.random.seed", JM_RT_SEED},
                                                          {"builtin.random.value", JM_RT_RANDOM},
                                                          {"builtin.random.int", JM_RT_RANDOM_INT},
                                                          {"builtin.random.float", JM_RT_RANDOM_FLOAT}};
    auto found = operations.find(std::string(name));
    return found == operations.end() ? 0 : found->second;
}

bool standardModule(std::string_view identity) {
    return identity == "math" || identity == "console" || identity == "collections" ||
           identity == "jm.random" || identity == "jm.time" || identity == "jm.io" || identity == "jm.math" ||
           identity == "jm.console" || identity == "jm.collections";
}
std::optional<StandardFunction> standardFunction(std::string_view name) {
    if (name == "readLine" || name == "builtin.console.readLine")
        return StandardFunction{std::string(name), "jm.console", "Read one line from the console.", 0, 0, Type::String,
                                false};
    if (auto op = runtimeStandardOperation(name)) {
        size_t arity = op == JM_RT_ATAN2 || op == JM_RT_RANDOM_INT || op == JM_RT_RANDOM_FLOAT  ? 2
                       : op == JM_RT_LERP || op == JM_RT_SMOOTHSTEP                             ? 3
                       : op == JM_RT_RANDOM || op == JM_RT_TIME_NOW || op == JM_RT_TIME_ELAPSED ? 0
                                                                                                : 1;
        Type result = op == JM_RT_SEED ? Type::Void : op == JM_RT_RANDOM_INT ? Type::Int : Type::Float;
        return StandardFunction{std::string(name),
                                op >= JM_RT_SEED ? "jm.random" : "jm.math",
                                "Runtime standard library.",
                                arity,
                                arity,
                                result,
                                op != JM_RT_SEED && op != JM_RT_RANDOM_INT};
    }
    if (name.starts_with("builtin.math."))
        name.remove_prefix(13);
    if (name == "sqrt" || name == "sin" || name == "cos" || name == "floor" || name == "ceil" ||
        name == "round" || name == "abs")
        return StandardFunction{
            "builtin.math." + std::string(name),    "jm.math", "Numeric standard library function.", 1, 1,
            name == "abs" ? Type::Any : Type::Float};
    if (name == "pow")
        return StandardFunction{"builtin.math.pow", "jm.math", "Raise base to exponent.", 2, 2, Type::Float};
    if (name == "clamp")
        return StandardFunction{
            "builtin.math.clamp", "jm.math", "Clamp value between inclusive bounds.", 3, 3, Type::Float};
    if (name == "min" || name == "max")
        return StandardFunction{"builtin.math." + std::string(name),     "jm.math",
                                "Minimum/maximum of numeric arguments.", 1,
                                std::numeric_limits<std::size_t>::max(), Type::Any};
    return std::nullopt;
}
} // namespace jm::script
