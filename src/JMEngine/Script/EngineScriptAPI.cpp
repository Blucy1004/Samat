#include "JMEngine/Script/EngineScriptAPI.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace jm {
namespace {
thread_local EngineScriptContext *active{};
float finiteFloat(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::runtime_error("Engine scalar is outside finite Float32 range.");
    return static_cast<float>(value);
}
using I64 = std::int64_t;
I64 move(EngineScriptContext &context, I64 direction, I64 speed) {
    auto *player = context.player();
    if (!player)
        return -1;
    player->position.x += static_cast<float>(direction * static_cast<double>(speed) * context.deltaSeconds);
    return 1;
}
I64 jump(EngineScriptContext &context, I64 force) {
    auto *player = context.player();
    if (!player)
        return -1;
    if (!player->grounded)
        return 0;
    player->verticalVelocity = finiteFloat(force);
    player->grounded = false;
    return 1;
}
I64 moveThunk(I64 direction, I64 speed, I64, I64) { return active ? move(*active, direction, speed) : -1; }
I64 jumpThunk(I64 force, I64, I64, I64) { return active ? jump(*active, force) : -1; }
I64 groundedThunk(I64, I64, I64, I64) {
    auto *p = active ? active->player() : nullptr;
    return p && p->grounded ? 1 : 0;
}
I64 deltaThunk(I64, I64, I64, I64) {
    return active ? static_cast<I64>(std::llround(active->deltaSeconds * 1'000'000.0)) : -1;
}
I64 positionThunk(I64, I64, I64, I64) {
    auto *p = active ? active->player() : nullptr;
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
GameObject *EngineScriptContext::player() const {
    for (auto &object : scene.objects())
        if (object.id == playerId)
            return &object;
    return nullptr;
}
EngineScriptScope::EngineScriptScope(EngineScriptContext &context) : previous_(active) { active = &context; }
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
    auto typed = [&](std::string symbol, std::vector<std::string> names, std::vector<Type> types, Type result,
                     Registry::TypedFunction function, std::string documentation) {
        registry.registerTypedFunction(
            {symbol, symbol.substr(8), symbol, documentation, names, types, result}, std::move(function));
    };
    typed(
        "builtin.player.moveFloat", {"direction", "speed"}, {Type::Float, Type::Float}, Type::Bool,
        [](const std::vector<script::Value> &args) {
            auto *player = active ? active->player() : nullptr;
            if (!player)
                return script::Value(false);
            auto direction = std::get<double>(args[0].data), speed = std::get<double>(args[1].data);
            if (!std::isfinite(direction) || !std::isfinite(speed))
                throw std::runtime_error("Engine movement requires finite numbers.");
            player->position.x = finiteFloat(player->position.x + direction * speed * active->deltaSeconds);
            return script::Value(true);
        },
        "Move using Float direction/speed and frame delta.");
    typed(
        "builtin.player.jumpFloat", {"force"}, {Type::Float}, Type::Bool,
        [](const std::vector<script::Value> &args) {
            auto *player = active ? active->player() : nullptr;
            if (!player || !player->grounded)
                return script::Value(false);
            auto force = std::get<double>(args[0].data);
            if (!std::isfinite(force))
                throw std::runtime_error("Jump force must be finite.");
            player->verticalVelocity = finiteFloat(force);
            player->grounded = false;
            return script::Value(true);
        },
        "Apply Float jump force when grounded.");
    typed(
        "builtin.time.delta", {}, {}, Type::Float,
        [](const std::vector<script::Value> &) { return script::Value(active ? active->deltaSeconds : 0.0); },
        "Frame delta in seconds.");
    typed(
        "builtin.transform.x", {}, {}, Type::Float,
        [](const std::vector<script::Value> &) {
            auto *player = active ? active->player() : nullptr;
            return script::Value(player ? static_cast<double>(player->position.x) : 0.0);
        },
        "Player X coordinate.");
    auto scalar = [](double value) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            throw std::runtime_error("Engine scalar is outside finite Float32 range.");
        return static_cast<float>(value);
    };
    typed(
        "builtin.transform.setPosition", {"x", "y"}, {Type::Float, Type::Float}, Type::Bool,
        [scalar](const auto &args) {
            auto *player = active ? active->player() : nullptr;
            if (!player)
                return script::Value(false);
            auto x = scalar(std::get<double>(args[0].data)), y = scalar(std::get<double>(args[1].data));
            player->position.x = x;
            player->position.y = y;
            return script::Value(true);
        },
        "Set actual player 2D position.");
    typed(
        "builtin.physics.setVelocityY", {"velocity"}, {Type::Float}, Type::Bool,
        [scalar](const auto &args) {
            auto *player = active ? active->player() : nullptr;
            if (!player)
                return script::Value(false);
            player->verticalVelocity = scalar(std::get<double>(args[0].data));
            return script::Value(true);
        },
        "Set player vertical velocity.");
    typed(
        "builtin.physics.applyImpulseY", {"impulse"}, {Type::Float}, Type::Bool,
        [scalar](const auto &args) {
            auto *player = active ? active->player() : nullptr;
            if (!player)
                return script::Value(false);
            if (!(player->mass > 0))
                throw std::runtime_error("Impulse requires positive mass.");
            player->verticalVelocity =
                scalar(player->verticalVelocity + std::get<double>(args[0].data) / player->mass);
            player->grounded = false;
            return script::Value(true);
        },
        "Apply impulse / mass to vertical velocity.");
    typed(
        "builtin.input.isHeld", {"key"}, {Type::String}, Type::Bool,
        [](const auto &args) {
            if (!active)
                return script::Value(false);
            auto &key = std::get<std::string>(args[0].data);
            return script::Value(key == "right"  ? active->input.rightHeld
                                 : key == "left" ? active->input.leftHeld
                                                 : active->input.keysHeld.contains(key));
        },
        "Read a named held key.");
    typed(
        "builtin.scene.find", {"name"}, {Type::String}, Type::String,
        [](const auto &args) {
            if (active)
                for (const auto &object : active->scene.objects())
                    if (object.name == std::get<std::string>(args[0].data))
                        return script::Value(object.id);
            return script::Value("");
        },
        "Find object ID by name; empty string when unavailable.");
    typed(
        "builtin.scene.createSprite", {"name"}, {Type::String}, Type::String,
        [](const auto &args) {
            if (!active)
                return script::Value("");
            auto &object = active->scene.create(ObjectKind::Sprite2D);
            object.name = std::get<std::string>(args[0].data);
            return script::Value(object.id);
        },
        "Create a Sprite2D and return its stable ID.");
    typed(
        "builtin.scene.destroy", {"id"}, {Type::String}, Type::Bool,
        [](const auto &args) {
            if (!active)
                return script::Value(false);
            auto id = std::get<std::string>(args[0].data);
            auto &objects = active->scene.objects();
            if (std::none_of(objects.begin(), objects.end(),
                             [&](const auto &object) { return object.id == id; }))
                return script::Value(false);
            auto previous = active->scene.selected() ? active->scene.selected()->id : std::string{};
            active->scene.select(id);
            active->scene.deleteSelected();
            active->scene.select(previous);
            return script::Value(true);
        },
        "Destroy an object by stable ID.");
    typed(
        "builtin.scene.count", {}, {}, Type::Int,
        [](const auto &) {
            return script::Value(static_cast<I64>(active ? active->scene.objects().size() : 0));
        },
        "Current scene object count.");
    return registry;
}
std::function<bool(std::string_view)> engineModuleResolver() {
    return [](std::string_view identity) { return identity == "jm.game"; };
}
script::HostFunction engineHostFunctions(EngineScriptContext &context) {
    return [&context](const std::string &symbol, const std::vector<script::Value> &arguments,
                      const std::vector<std::string> &names) -> script::Value {
        auto registry = engineNativeFunctions();
        const auto id = script::stableBuiltinSymbolId(symbol);
        auto function = registry.find(id);
        auto info = registry.metadata(id);
        auto typed = registry.typed(id);
        if ((!function && !typed) || !info)
            throw std::out_of_range("Unknown engine symbol");
        if (arguments.size() != info->parameterTypes.size())
            throw std::runtime_error("Engine function argument count mismatch: " + symbol);
        if (typed) {
            auto values = arguments;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i < names.size() && !names[i].empty() && names[i] != info->parameterNames[i])
                    throw std::runtime_error("Engine named argument order must match metadata.");
                if (info->parameterTypes[i] == script::Type::Float && values[i].type() == script::Type::Int)
                    values[i] = script::Value(static_cast<double>(std::get<I64>(values[i].data)));
                if (values[i].type() != info->parameterTypes[i])
                    throw std::runtime_error("Engine typed parameter mismatch.");
            }
            EngineScriptScope scope(context);
            return typed->function(values);
        }
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
void executeNativeEvent(const script::ir::Module &module, const script::ir::NativeCode &code,
                        const std::string &event) {
    for (const auto &binding : module.events)
        if (binding.event == event)
            (void)code.invokeValue(binding.function);
}
} // namespace jm

namespace jm {
EngineEventRuntime::EngineEventRuntime(Scene &scene, std::string playerId, script::Program program,
                                       EngineScriptBackend backend)
    : context_{scene, std::move(playerId), {}, 0.0} {
    if (backend == EngineScriptBackend::Interpreter) {
        script::RunOptions options;
        options.moduleResolver = engineModuleResolver();
        session_ = std::make_unique<script::ExecutionSession>(std::move(program), options,
                                                              engineHostFunctions(context_));
    } else {
        auto registry = engineNativeFunctions();
        script::ir::LoweringDiagnostic diagnostic;
        if (!script::ir::lower(program, module_, diagnostic, &registry))
            throw std::runtime_error(diagnostic.message);
        EngineScriptScope scope(context_);
        code_ = backend == EngineScriptBackend::LLVM ? script::ir::LLVMBackend{}.compile(module_, registry)
                                                     : script::ir::X64Backend{}.compile(module_, registry);
    }
}
void EngineEventRuntime::dispatch(const std::string &event) {
    EngineScriptScope scope(context_);
    if (session_)
        session_->dispatch(event);
    else
        executeNativeEvent(module_, code_, event);
}
void EngineEventRuntime::start() {
    if (started_)
        return;
    dispatch("start");
    started_ = true;
}
void EngineEventRuntime::tick(const ScriptInput &input, double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0)
        throw std::runtime_error("Engine delta must be finite and nonnegative.");
    context_.input = input;
    context_.deltaSeconds = deltaSeconds;
    start();
    dispatch("update");
    dispatch("fixedUpdate");
    if (input.rightHeld)
        dispatch("key.right.held");
    if (input.leftHeld)
        dispatch("key.left.held");
    if (input.spacePressed)
        dispatch("key.space.pressed");
    auto held = std::vector<std::string>(input.keysHeld.begin(), input.keysHeld.end()),
         pressed = std::vector<std::string>(input.keysPressed.begin(), input.keysPressed.end());
    std::sort(held.begin(), held.end());
    std::sort(pressed.begin(), pressed.end());
    for (const auto &key : held)
        if (key != "left" && key != "right")
            dispatch("key." + key + ".held");
    for (const auto &key : pressed)
        if (key != "space" || !input.spacePressed)
            dispatch("key." + key + ".pressed");
}
} // namespace jm
