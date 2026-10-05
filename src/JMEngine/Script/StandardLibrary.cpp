#include "JMEngine/Script/StandardLibrary.hpp"
#include <limits>

namespace jm::script {
bool standardModule(std::string_view identity) {
    return identity=="jm.math" || identity=="jm.console" || identity=="jm.collections";
}
std::optional<StandardFunction> standardFunction(std::string_view name) {
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
