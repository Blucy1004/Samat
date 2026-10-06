#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#if JMENGINE_HAS_ENGINE
#include "JMEngine/Script/EngineScriptAPI.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace jm::script;
using namespace jm::script::ir;
namespace {
size_t checks{}, unavailable{};
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
Program parse(const std::string &source) {
    Program program, korean, code;
    Diagnostic diagnostic;
    std::vector<Diagnostic> errors;
    require(parseCode(source, program, diagnostic), diagnostic.message);
    const auto checked = check(program, errors);
    require(checked, errors.empty() ? "Type check" : errors.front().code + ": " + errors.front().message);
    require(parseKorean(renderKorean(program), korean, diagnostic), diagnostic.message);
    require(structurallyEqual(program, korean), "Code/Korean AST mismatch");
    require(parseCode(renderCode(korean), code, diagnostic) && structurallyEqual(program, code),
            "Formatting changed semantics");
    return program;
}
void parity(const std::string &source, int64_t expected) {
    auto program = parse(source);
    RunOptions options;
    options.entryFunction = "main";
    require(std::get<int64_t>(execute(program, options).returnValue.data) == expected,
            "Interpreter Optional result");
    Module module;
    LoweringDiagnostic diagnostic;
    require(lower(program, module, diagnostic), diagnostic.message);
    if (LLVMBackend::available())
        for (bool optimized : {false, true}) {
            auto code = LLVMBackend{optimized}.compile(module);
            require(code.invoke("main") == expected, "LLVM Optional parity");
        }
    else
        ++unavailable;
    ++checks;
}
void negative(const std::string &source, const std::string &code) {
    Program program;
    Diagnostic diagnostic;
    std::vector<Diagnostic> errors;
    require(parseCode(source, program, diagnostic), diagnostic.message);
    require(!check(program, errors), "Unsafe program passed checking");
    require(std::any_of(errors.begin(), errors.end(), [&](const auto &error) { return error.code == code; }),
            "Missing expected diagnostic " + code);
    ++checks;
}
std::string live(double speed, int force = 12) {
    return "import jm.game\nlet speed: Float = " + std::to_string(speed) +
           "\nlet health: Int = 100\nlet presses: Int = 0\nlet extra: String = "
           "scene.createSprite(\"Extra\")\nfn jump() -> Void:\n "
           "   player.jump(force: " +
           std::to_string(force) +
           ")\non start:\n    health = 37\non update:\n    player.position += Vector2(speed * time.delta, "
           "0.0)\non key.space.pressed:\n    presses += 1\n    jump()\n";
}
void coreLive() {
    auto initial = parse(
        "let health: Int = 100\nlet speed: Int = 8\nfn main() -> Int:\n    health -= 1\n    return speed\n");
    ExecutionSession session(initial);
    require(session.invoke("main").returnValue.toString() == "8", "Initial live value");
    Diagnostic diagnostic;
    auto candidate = parse(
        "let health: Int = 100\nlet speed: Int = 15\nfn main() -> Int:\n    health -= 2\n    return speed\n");
    require(session.hotSwap(candidate, diagnostic), diagnostic.message);
    auto result = session.invoke("main");
    require(result.returnValue.toString() == "15" && result.globals->get("health").toString() == "97",
            "Live state preservation");
    require(session.inspect().at("health") == "Int: 97", "Runtime inspection");
    auto incompatible =
        parse("let health: Int = 100\nlet speed: Int = 15\nfn main(value: Int) -> Int:\n    return value\n");
    require(!session.hotSwap(incompatible, diagnostic) && diagnostic.code == "JM7202",
            "Signature mismatch rejection");
    require(session.generation() == 2, "Failed swap advanced generation");
    for (int i = 0; i < 100; ++i)
        require(session.hotSwap(candidate, diagnostic), diagnostic.message);
    require(session.generation() == 102, "100 core swaps");
    ++checks;
}
#if JMENGINE_HAS_ENGINE
void engineLive() {
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available()) {
            ++unavailable;
            continue;
        }
        jm::Scene scene;
        auto id = scene.objects()[1].id;
        auto before = scene.objects()[1].position.x;
        auto objectCount = scene.objects().size();
        jm::EngineEventRuntime runtime(scene, id, parse(live(8)), backend);
        runtime.start();
        runtime.tick({}, 0.25);
        Diagnostic diagnostic;
        require(runtime.hotSwap(parse(live(15, 20)), diagnostic), diagnostic.message);
        runtime.tick({}, 0.25);
        require(std::abs(scene.objects()[1].position.x - before - 5.75) < 1e-5, "Live speed 8 -> 15");
        require(runtime.inspect().at("health") == "Int: 37", "Health preserved across live swap");
        scene.objects()[1].grounded = true;
        jm::ScriptInput input;
        input.spacePressed = true;
        runtime.tick(input, 0);
        require(scene.objects()[1].verticalVelocity == 20, "New jump body/event installed");
        auto invalid = parse(live(15));
        invalid.statements[1].declaredType = Type::String;
        require(!runtime.hotSwap(invalid, diagnostic), "Invalid candidate replaced valid program");
        scene.objects()[1].grounded = true;
        runtime.tick(input, 0);
        require(scene.objects()[1].verticalVelocity == 20, "Previous valid generation lost");
        auto readyCandidate = parse(live(15, 20));
        auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i)
            require(runtime.hotSwap(readyCandidate, diagnostic), diagnostic.message);
        std::cout
            << "Debug hot swap mean (validation/compile/state transfer/swap): "
            << (backend == jm::EngineScriptBackend::LLVM ? "LLVM" : "Interpreter") << " "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() /
                   100
            << " ms\n";
        scene.objects()[1].grounded = true;
        runtime.tick(input, 0);
        require(scene.objects()[1].verticalVelocity == 20, "Repeated event replacement");
        require(runtime.inspect().at("health") == "Int: 37", "100 swaps preserve globals");
        require(scene.objects().size() == objectCount + 1, "Hot reload repeated module initialization");
        require(runtime.inspect().at("presses") == "Int: 3", "Duplicate event handlers accumulated");
        ++checks;
    }
}
void metadata() {
    auto registry = jm::engineNativeFunctions();
    auto jump = registry.metadata(stableBuiltinSymbolId("builtin.player.jump"));
    require(jump && metadataMatches(*jump, "ㅈㅍ"), "Hangul initial search");
    require(metadataSignature(*jump).find("force: Int") != std::string::npos, "Metadata signature");
    for (const auto &entry : registry.allMetadata()) {
        auto code = metadataTemplate(entry);
        if (code.empty())
            continue;
        auto program = parse(code);
        Diagnostic diagnostic;
        Program korean;
        require(parseKorean(metadataTemplate(entry, true), korean, diagnostic) &&
                    structurallyEqual(program, korean),
                "Metadata template parity");
    }
    Diagnostic diagnostic;
    Program beginner;
    require(parseKorean("시작할 때:\n    \"enemy\"를 enemy로 찾았다면:\n        enemy.position을 "
                        "Vector2(1.0, 0.0)만큼 늘린다.\n",
                        beginner, diagnostic),
            diagnostic.message);
    auto advanced = parse("on start:\n    let enemy: Entity? = scene.find(\"enemy\")\n    if enemy != "
                          "null:\n        enemy.position += Vector2(1.0, 0.0)\n");
    require(structurallyEqual(beginner, advanced), "Beginner lookup uses different AST");
    ++checks;
    for (const auto &[source, expected] : std::vector<std::pair<std::string, std::string>>{
             {"on start:\n    let enemy = scene.find(\"Player\")\n    enemy.destroy()\n", "JM2011"},
             {"on start:\n    let enemy = scene.find(42)\n", "JM2003"},
             {"on start:\n    let enemy: Entity? = scene.find(\"Player\")\n    if enemy != null:\n        "
              "enemy.setPosition(42)\n",
              "JM2003"}}) {
        Program unsafe;
        std::vector<Diagnostic> errors;
        require(parseCode(source, unsafe, diagnostic), diagnostic.message);
        require(!check(unsafe, errors, &registry) &&
                    std::any_of(errors.begin(), errors.end(),
                                [&](const auto &error) { return error.code == expected; }),
                "Registry-derived engine type diagnostic: " + expected);
        ++checks;
    }
}
void entitySafety() {
    auto program =
        parse("import jm.game\nfn main() -> Int:\n    let enemy: Entity? = scene.find(\"Player\")\n    if "
              "enemy != null:\n        enemy.position += Vector2(1.0, 0.0)\n        enemy.destroy()\n    let "
              "missing: Entity? = scene.find(\"Player\")\n    missing?.destroy()\n    if missing == null:\n  "
              "      return 42\n    return 0\n");
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available()) {
            ++unavailable;
            continue;
        }
        jm::Scene scene;
        auto count = scene.objects().size();
        jm::EngineScriptContext context{scene, scene.objects()[1].id};
        auto host = jm::engineHostFunctions(context);
        auto handle = host("builtin.scene.find", {Value("Player")}, {}).unwrap();
        jm::Scene copied = scene;
        jm::EngineScriptContext copiedContext{copied, copied.objects()[1].id};
        bool wrongScene = false;
        try {
            jm::engineHostFunctions(copiedContext)("builtin.entity.position", {handle}, {});
        } catch (const std::exception &error) {
            wrongScene = std::string(error.what()).find("JM7102") != std::string::npos;
        }
        require(wrongScene, "Copied Scene accepted original Entity handle");
        RunOptions options;
        options.entryFunction = "main";
        options.moduleResolver = jm::engineModuleResolver();
        if (backend == jm::EngineScriptBackend::Interpreter)
            require(execute(program, options, host).returnValue.toString() == "42",
                    "Entity Interpreter execution");
        else {
            auto registry = jm::engineNativeFunctions();
            Module module;
            LoweringDiagnostic diagnostic;
            require(lower(program, module, diagnostic, &registry), diagnostic.message);
            jm::EngineScriptScope scope(context);
            auto code = LLVMBackend{}.compile(module, registry);
            require(code.invoke("main") == 42, "Entity LLVM execution");
        }
        require(scene.objects().size() == count - 1, "Entity destroy did not change Scene");
        bool stale = false;
        try {
            host("builtin.entity.position", {handle}, {});
        } catch (const std::exception &error) {
            stale = std::string(error.what()).find("JM7102") != std::string::npos;
        }
        require(stale, "Destroyed Entity access did not fail safely");
        auto &replacement = scene.create(jm::ObjectKind::Sprite2D);
        replacement.id = std::get<EntityReference>(handle.data).id;
        replacement.name = "Player";
        stale = false;
        try {
            host("builtin.entity.position", {handle}, {});
        } catch (const std::exception &) {
            stale = true;
        }
        require(stale, "ID reuse resurrected stale handle");
        auto fresh = host("builtin.scene.find", {Value("Player")}, {}).unwrap();
        scene.replaceObjects(scene.objects());
        stale = false;
        try {
            host("builtin.entity.position", {fresh}, {});
        } catch (const std::exception &) {
            stale = true;
        }
        require(stale, "Scene reset did not invalidate handles");
        ++checks;
    }
}
void boundedInspection() {
    std::string source;
    for (int i = 0; i < 80; ++i)
        source += "let text" + std::to_string(i) + ": String = \"snapshot\"\n";
    auto program = parse(source);
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available()) {
            ++unavailable;
            continue;
        }
        jm::Scene scene;
        jm::EngineEventRuntime runtime(scene, scene.objects()[1].id, program, backend);
        runtime.start();
        auto snapshot = runtime.inspect();
        require(snapshot.size() == 67 && snapshot.contains("player.position"),
                "Inspection must bound String globals to 64 rows plus three player values");
        ++checks;
    }
}
#endif
} // namespace
int main() {
    try {
        for (int seed = 0; seed < 32; ++seed)
            parity("fn main() -> Int:\n    let value: Int? = " +
                       std::string(seed % 2 ? std::to_string(seed) : "null") +
                       "\n    let result = value ?? 42\n    return result\n",
                   seed % 2 ? seed : 42);
        parity("fn main() -> Int:\n    let value: Int? = 41\n    if value != null:\n        return value + "
               "1\n    return 0\n",
               42);
        parity("fn main() -> Int:\n    let value: Int? = 41\n    if value == null:\n        return 0\n    "
               "return value + 1\n",
               42);
        parity("fn main() -> Int:\n    let value: String? = \"abc\"\n    return value?.length ?? 42\n", 3);
        parity("fn main() -> Int:\n    let value: Vector2? = null\n    let number: Float = value?.x ?? "
               "42.0\n    return int(number)\n",
               42);
        parity("fn main() -> Int:\n    let value: Bool? = false\n    if value == null:\n        return 0\n   "
               " return 42\n",
               42);
        negative("let x: Int = null\n", "JM2001");
        negative("let x: Int? = \"wrong\"\n", "JM2001");
        negative("let x: Int? = null\nlet y = x + 1\n", "JM2011");
        negative("let x: Int? = null\nlet y = x.position\n", "JM2011");
        negative("let x: Int = 1\nlet y = x ?? 2\n", "JM2010");
        negative("let x: Int = 1\nlet y = x?.length\n", "JM2010");
        parity("fn main() -> Int:\n    let value: Float? = 41.5\n    let answer = value ?? 0.0\n    return "
               "int(answer + 0.5)\n",
               42);
        parity("struct Stats:\n    health: Int\nfn main() -> Int:\n    let state: Stats? = Stats(health: "
               "42)\n    if state != null:\n        return state.health\n    return 0\n",
               42);
        parity("struct Stats:\n    health: Int\nfn main() -> Int:\n    let state: Stats? = null\n    return "
               "state?.health ?? 42\n",
               42);
        parity("struct Stats:\n    health: Int? = null\nfn main() -> Int:\n    let state = Stats()\n    "
               "return state.health ?? 42\n",
               42);
        {
            ExecutionSession repl(Program{});
            repl.evaluate("let value: Int? = null\n");
            require(repl.evaluate("value ?? 42\n").returnValue.toString() == "42",
                    "Persistent Optional REPL");
            ++checks;
        }
        negative("struct A:\n    x: Int\nstruct B:\n    x: Int\nlet value: A? = B(x: 1)\n", "JM2010");
        {
            NativeFunctionRegistry registry;
            registry.registerTypedFunction(
                {"builtin.host.maybe",
                 "host.maybe",
                 "값",
                 "optional result",
                 {},
                 {},
                 Type::Optional,
                 {},
                 Type::Int},
                [](const auto &) { return Value::optional(Type::Int, Value(42)); });
            auto program =
                parse("fn main() -> Int:\n    let value: Int? = host.maybe()\n    return value ?? 0\n");
            Module module;
            LoweringDiagnostic diagnostic;
            require(lower(program, module, diagnostic, &registry), diagnostic.message);
            if (LLVMBackend::available()) {
                auto code = LLVMBackend{}.compile(module, registry);
                require(code.invoke("main") == 42, "Optional FFI result");
            } else
                ++unavailable;
            ++checks;
        }
        for (int seed = 0; seed < 32; ++seed)
            parity("fn choose(value: Int?) -> Int:\n    return value ?? 2\nfn main() -> Int:\n    let value: "
                   "Int? = " +
                       std::string(seed % 2 ? std::to_string(seed) : "null") +
                       "\n    let sum: Int = 0\n    for i in 0..5:\n        sum += choose(value)\n    return "
                       "sum\n",
                   seed % 2 ? seed * 5 : 10);
        {
            auto context = jm_runtime_create_context();
            auto previous = jm_runtime_activate(context);
            for (int i = 0; i < 1000; ++i) {
                auto string = jm_string_create("owned", 5);
                auto optional = jm_runtime_call(JM_RT_OPTIONAL_SOME, JM_RT_STRING, string, 0);
                auto record = jm_runtime_call(JM_RT_RECORD_CREATE, JM_RT_STRUCT, 0, 0);
                jm_runtime_call(JM_RT_RECORD_APPEND, record, optional, JM_RT_OPTIONAL);
                jm_runtime_retain(record);
                jm_runtime_collect();
                require(jm_runtime_live_objects() == 3, "Optional child was reclaimed");
                auto box =
                    jm_runtime_call(JM_RT_RECORD_GET, record, static_cast<uint64_t>(JM_RT_OPTIONAL) << 32, 0);
                auto payload = jm_runtime_call(JM_RT_OPTIONAL_GET, box, JM_RT_STRING, 0);
                uint64_t size;
                require(std::string(jm_string_bytes(payload, &size), 5) == "owned",
                        "Optional String lifetime");
                jm_runtime_release(record);
                jm_runtime_collect();
                require(jm_runtime_live_objects() == 0, "Optional ownership leak");
            }
            jm_runtime_activate(previous);
            jm_runtime_destroy_context(context);
            ++checks;
        }
        {
            auto program = parse("fn main() -> Int:\n    let value: Int? = null\n    return value ?? 42\n");
            Module module;
            LoweringDiagnostic diagnostic;
            require(lower(program, module, diagnostic), diagnostic.message);
            bool rejected = false;
            try {
                auto code = X64Backend{}.compile(module);
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "Bootstrap silently accepted Optional");
            bool mutated = false;
            for (auto &function : module.functions)
                for (auto &block : function.blocks)
                    for (auto &instruction : block.instructions)
                        if (instruction.op == Op::RuntimeCall &&
                            instruction.immediate == JM_RT_OPTIONAL_NONE) {
                            function.valueElementTypes[instruction.result] = Type::Float;
                            mutated = true;
                        }
            require(mutated && !verify(module, diagnostic) &&
                        diagnostic.message.find("JM4001") != std::string::npos,
                    "Invalid Optional IR passed verifier");
            ++checks;
        }
        parity("fn main() -> Int:\n    let value: Int? = 41\n    if value != null:\n        return -value + "
               "83\n    return 0\n",
               42);
        parity("fn main() -> Int:\n    let first: String? = null\n    let second: String? = null\n    if "
               "first == second and null == first:\n        return 42\n    return 0\n",
               42);
        parity("fn main() -> Int:\n    let value: Bool? = false\n    if value == false and value != null:\n  "
               "      return 42\n    return 0\n",
               42);
        parity("let touches: Int = 0\nfn fallback() -> Int:\n    touches += 1\n    return 0\nfn main() -> "
               "Int:\n    let value: Int? = 42\n    let answer = value ?? fallback()\n    return answer + "
               "touches\n",
               42);
        coreLive();
#if JMENGINE_HAS_ENGINE
        metadata();
        engineLive();
        entitySafety();
        boundedInspection();
#endif
        std::cout << "Safe & Live regression: " << checks << " checks PASS, " << unavailable
                  << " unavailable backend groups\n";
    } catch (const std::exception &error) {
        std::cerr << "Safe & Live regression failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
