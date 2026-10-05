#include "JMEngine/Script/EngineScriptAPI.hpp"
#include <cmath>

namespace jm {
namespace {
thread_local EngineScriptContext* active{};
using I64 = std::int64_t;
I64 move(EngineScriptContext& context, I64 direction, I64 speed) {
    auto* player = context.player();
    if (!player)
        return -1;
    player->position.x += static_cast<float>(direction * static_cast<double>(speed) * context.deltaSeconds);
    return 1;
}
I64 jump(EngineScriptContext& context, I64 force) {
    auto* player = context.player();
    if (!player)
        return -1;
    if (!player->grounded)
        return 0;
    player->verticalVelocity = static_cast<float>(force);
    player->grounded = false;
    return 1;
}
I64 moveThunk(I64 direction, I64 speed, I64, I64) { return active ? move(*active, direction, speed) : -1; }
I64 jumpThunk(I64 force, I64, I64, I64) { return active ? jump(*active, force) : -1; }
I64 groundedThunk(I64, I64, I64, I64) {
    auto* p = active ? active->player() : nullptr;
    return p && p->grounded ? 1 : 0;
}
I64 deltaThunk(I64, I64, I64, I64) {
    return active ? static_cast<I64>(std::llround(active->deltaSeconds * 1'000'000.0)) : -1;
}
I64 positionThunk(I64, I64, I64, I64) {
    auto* p = active ? active->player() : nullptr;
    return p ? static_cast<I64>(std::llround(p->position.x * 1000.0)) : -1;
}
I64 heldThunk(I64 key, I64, I64, I64) {
    if (!active)
        return -1;
    return key == 0   ? active->input.leftHeld
           : key == 1 ? active->input.rightHeld
           : key == 2 ? active->input.keysHeld.contains("space")
                      : false;
}
I64 pressedThunk(I64 key, I64, I64, I64) {
    if (!active)
        return -1;
    return key == 2   ? active->input.spacePressed
           : key == 0 ? active->input.keysPressed.contains("left")
           : key == 1 ? active->input.keysPressed.contains("right")
                      : false;
}
} // namespace
GameObject* EngineScriptContext::player() const {
    for (auto& object : scene.objects())
        if (object.id == playerId)
            return &object;
    return nullptr;
}
EngineScriptScope::EngineScriptScope(EngineScriptContext& context) : previous_(active) { active = &context; }
EngineScriptScope::~EngineScriptScope() { active = previous_; }
script::ir::NativeFunctionRegistry engineNativeFunctions() {
    using script::Type;
    using Registry = script::ir::NativeFunctionRegistry;
    Registry registry;
    registry.registerModule("jm.game");
    auto add = [&](std::string symbol, std::string korean, std::vector<std::string> names,
                   Registry::Function function, std::string documentation) {
        std::vector<Type> types(names.size(), Type::Int);
        registry.registerFunction({symbol, symbol.substr(8), korean, documentation, names, types, Type::Int},
                                  function);
    };
    add("builtin.player.move", "플레이어.이동", {"direction", "speed"}, moveThunk,
        "Move X by direction * speed * frame delta; returns 1, or -1 if no player/context.");
    add("builtin.player.jump", "플레이어.점프", {"force"}, jumpThunk,
        "Jump only when grounded; returns 1 on jump, 0 when airborne, -1 if unavailable.");
    add("builtin.player.grounded", "플레이어.접지", {}, groundedThunk, "Grounded state as 0/1.");
    add("builtin.input.keyHeld", "입력.유지", {"key"}, heldThunk,
        "Key IDs: 0 left, 1 right, 2 space; returns 0/1.");
    add("builtin.input.keyPressed", "입력.눌림", {"key"}, pressedThunk,
        "Key IDs: 0 left, 1 right, 2 space; returns 0/1.");
    add("builtin.time.deltaMicros", "시간.간격", {}, deltaThunk, "Frame delta in integer microseconds.");
    add("builtin.transform.xMilli", "변환.x", {}, positionThunk, "Player X position in integer mill units.");
    return registry;
}
std::function<bool(std::string_view)> engineModuleResolver() {
    return [](std::string_view identity) { return identity=="jm.game"; };
}
script::HostFunction engineHostFunctions(EngineScriptContext& context) {
    return [&context](const std::string& symbol, const std::vector<script::Value>& arguments,
                      const std::vector<std::string>& names) -> script::Value {
        auto registry = engineNativeFunctions();
        const auto id = script::stableBuiltinSymbolId(symbol);
        auto function = registry.find(id);
        auto info = registry.metadata(id);
        if (!function || !info)
            throw std::out_of_range("Unknown engine symbol");
        if (arguments.size() != info->parameterTypes.size())
            throw std::runtime_error("Engine function argument count mismatch: " + symbol);
        I64 values[4]{};
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (arguments[i].type() != script::Type::Int)
                throw std::runtime_error("Engine scalar bridge requires Int parameters: " + symbol);
            if (i < names.size() && !names[i].empty() && names[i] != info->parameterNames[i])
                throw std::runtime_error("Engine named argument order must match registry metadata.");
            values[i] = std::get<I64>(arguments[i].data);
        }
        EngineScriptScope scope(context);
        return script::Value(function(values[0], values[1], values[2], values[3]));
    };
}
void executeNativeEvent(const script::ir::Module& module, const script::ir::NativeCode& code,
                        const std::string& event) {
    for (const auto& binding : module.events)
        if (binding.event == event)
            (void)code.invokeValue(binding.function);
}
} // namespace jm
