#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#if JMENGINE_HAS_ENGINE
#include "JMEngine/Script/EngineScriptAPI.hpp"
#endif
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#ifndef _WIN32
#include <sys/wait.h>
#endif
using namespace jm::script;
using namespace jm::script::ir;
namespace {
size_t passed{}, skipped{};
void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Program parse(const std::string &source) {
    Program p;
    Diagnostic d;
    require(parseCode(source, p, d), d.message);
    std::vector<Diagnostic> errors;
    require(check(p, errors), errors.empty() ? "Type check" : errors.front().message);
    Program korean, formatted;
    require(parseKorean(renderKorean(p), korean, d), d.message);
    require(structurallyEqual(p, korean), "Korean semantic AST round trip");
    require(parseCode(renderCode(p), formatted, d) && structurallyEqual(p, formatted),
            "Formatter round trip");
    return p;
}
Module lowerChecked(const Program &p, const NativeFunctionRegistry *registry = nullptr) {
    Module m;
    LoweringDiagnostic d;
    require(lower(p, m, d, registry), d.message);
    return m;
}
void parity(const std::string &source, double expected) {
    auto p = parse(source);
    RunOptions options;
    options.entryFunction = "main";
    auto compare = [&](Value value) {
        auto actual = value.type() == Type::Float ? std::get<double>(value.data)
                                                  : static_cast<double>(std::get<int64_t>(value.data));
        require(std::abs(actual - expected) <= 1e-9 * std::max(1.0, std::abs(expected)),
                "Differential numeric result");
    };
    compare(execute(p, options).returnValue);
    auto m = lowerChecked(p);
    if (LLVMBackend::available())
        for (bool optimized : {false, true})
            compare(LLVMBackend{optimized}.compile(m).invokeValue("main"));
    else
        ++skipped;
    ++passed;
}
const std::string record = R"(struct Stats:
    health: Int
    speed: Vector2
    name: String
    scores: List<Int>
let state = Stats(health: 100, speed: Vector2(8.0, 0.0), name: "Player", scores: [1, 2])
fn main() -> Int:
    let alias = state
    alias.health -= 58
    state.scores.push(3)
    let velocity = state.speed * 0.5
    if velocity.x == 4.0 and state.name == "Player" and state.scores.length == 3:
        return state.health
    return 1
)";
const std::string maps = R"(fn main() -> Int:
    let scores: Map<String, Int> = {"JM": 10, "Engine": 20}
    scores["JM"] = 42
    if scores.containsKey("JM") and scores.length == 2:
        let keys = scores.keys()
        let values = scores.values()
        if keys.length == 2 and values.length == 2:
            let result = scores["JM"]
            scores.remove("Engine")
            scores.clear()
            return result + scores.length
    return 1
)";
void memory() {
    auto context = jm_runtime_create_context();
    auto previous = jm_runtime_activate(context);
    for (int i = 0; i < 1000; ++i) {
        auto vector = jm_runtime_call(JM_RT_AGGREGATE_CREATE, JM_RT_VECTOR2, 0, 0);
        jm_runtime_call(JM_RT_AGGREGATE_APPEND, vector, std::bit_cast<uint64_t>(3.0), JM_RT_VECTOR2);
        jm_runtime_call(JM_RT_AGGREGATE_APPEND, vector, std::bit_cast<uint64_t>(4.0), JM_RT_VECTOR2);
        require(std::bit_cast<double>(jm_runtime_call(JM_RT_VECTOR_LENGTH, vector, 0, 0)) == 5.0,
                "Sanitized vector runtime length");
        jm_runtime_collect();
        require(jm_runtime_live_objects() == 0, "Vector temporary collection");
        auto text = jm_string_create("JM", 2);
        auto list = jm_runtime_call(JM_RT_LIST_CREATE, JM_RT_STRING, 0, 0);
        jm_runtime_call(JM_RT_LIST_PUSH, list, text, JM_RT_STRING);
        auto record = jm_runtime_call(JM_RT_RECORD_CREATE, JM_RT_STRUCT, 0, 0);
        jm_runtime_call(JM_RT_RECORD_APPEND, record, list, JM_RT_LIST);
        jm_runtime_retain(record);
        jm_runtime_collect();
        require(jm_runtime_live_objects() == 3, "Record child tracing");
        auto encoded = (static_cast<uint64_t>(JM_RT_LIST) << 32);
        require(jm_runtime_call(JM_RT_RECORD_GET, record, encoded, 0) == list, "Retained record read");
        jm_runtime_release(record);
        jm_runtime_collect();
        require(jm_runtime_live_objects() == 0, "Record release collection");
        auto map = jm_runtime_call(JM_RT_MAP_CREATE, JM_RT_STRING, 0, 0);
        auto key = jm_string_create("key", 3);
        auto value = jm_string_create("value", 5);
        jm_runtime_call(JM_RT_MAP_SET, map, key, value);
        jm_runtime_retain(map);
        jm_runtime_collect();
        require(jm_runtime_live_objects() == 2, "Map owns key bytes and String values");
        jm_runtime_release(map);
        jm_runtime_collect();
        require(jm_runtime_live_objects() == 0, "Map collection");
    }
    bool rejected = false;
    try {
        jm_runtime_require_abi(JM_RUNTIME_ABI_VERSION + 1);
    } catch (const std::exception &e) {
        rejected = std::string(e.what()).find("JM6003") != std::string::npos;
    }
    require(rejected, "ABI mismatch diagnostic");
    jm_runtime_activate(previous);
    jm_runtime_destroy_context(context);
    ++passed;
}
void engine() {
#if JMENGINE_HAS_ENGINE
    auto p = parse(R"(import jm.game
struct State:
    velocity: Vector2
let state = State(velocity: Vector2(8.0, 0.0))
on update:
    player.position += state.velocity * time.delta
)");
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available()) {
            ++skipped;
            continue;
        }
        for (int cycle = 0; cycle < 50; ++cycle) {
            jm::Scene scene;
            auto &object = scene.create(jm::ObjectKind::Sprite2D);
            object.name = "Player";
            auto id = object.id;
            auto start = object.position.x;
            {
                jm::EngineEventRuntime runtime(scene, id, p, backend);
                runtime.start();
                runtime.start();
                for (int frame = 0; frame < 100; ++frame)
                    runtime.tick({}, 0.01);
            }
            auto found = std::find_if(scene.objects().begin(), scene.objects().end(),
                                      [&](const auto &o) { return o.id == id; });
            require(found != scene.objects().end() && std::abs(found->position.x - start - 8.0) < 0.001,
                    "Persistent Vector engine movement");
        }
        ++passed;
    }
#else
    ++skipped;
#endif
}
void aot() {
#ifndef _WIN32
    if (!LLVMBackend::available()) {
        ++skipped;
        return;
    }
    auto directory =
        std::filesystem::temp_directory_path() /
        ("jm-v06-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{directory};
    std::vector<std::string> sources{
        record, maps, "fn main() -> Int:\n    let v = Vector2(3.0, 4.0)\n    return int(v.length) + 37\n",
        "fn main() -> Int:\n    let total = 0\n    for i in range(6, 0, -2):\n        total += i\n    let t "
        "= (30, 1.5, \"JM\")\n    return total + t.0\n"};
    for (size_t i = 0; i < sources.size(); ++i) {
        auto path = directory / std::to_string(i);
        LLVMBackend{true}.build(lowerChecked(parse(sources[i])), path.string());
        auto status = std::system(path.c_str());
        require(WIFEXITED(status) && WEXITSTATUS(status) == 42, "AOT actual exit 42");
        ++passed;
    }
#else
    ++skipped;
#endif
}
} // namespace
int main() {
    try {
        parity(record, 42);
        parity(maps, 42);
        parity("fn main() -> Float:\n    let a = Vector2(3.0, 4.0)\n    return a.length\n", 5);
        parity("fn main() -> Float:\n    let a = Vector2(3.0, 4.0)\n    let b = 2.0 * a\n    return "
               "b.normalized.length\n",
               1);
        parity("fn main() -> Float:\n    let a = Vector3(1.0, 0.0, 0.0)\n    let b = Vector3(0.0, 1.0, "
               "0.0)\n    return a.cross(b).z\n",
               1);
        parity("fn main() -> Float:\n    let c = Color(1.0, 0.5, 0.0, 1.0)\n    return c.g\n", 0.5);
        parity("fn main() -> Int:\n    let t = (10, 20.5, \"JM\")\n    return t.0 + int(t.1) + t.2.length\n",
               32);
        parity("fn main() -> Int:\n    let total = 0\n    for i in range(6, 0, -2):\n        total += i\n    "
               "return total\n",
               12);
        std::mt19937 random(606);
        for (int seed = 0; seed < 32; ++seed) {
            auto x = static_cast<int>(random() % 101) - 50, y = static_cast<int>(random() % 101) - 50;
            auto scale = static_cast<int>(random() % 9) + 1;
            auto source = "fn main() -> Float:\n    let a = Vector2(" + std::to_string(x) + ".0, " +
                          std::to_string(y) + ".0)\n    let v = a * " + std::to_string(scale) +
                          ".0\n    return v.x + v.y\n";
            try {
                parity(source, (x + y) * scale);
            } catch (const std::exception &e) {
                throw std::runtime_error("seed=" + std::to_string(seed) + "\n" + source + e.what());
            }
        }
        {
            Program p;
            Diagnostic d;
            require(parseCode("struct Stats:\n    health: Int\nfn main():\n    let a = Stats(health: 1)\n    "
                              "return a.hp\n",
                              p, d),
                    d.message);
            std::vector<Diagnostic> errors;
            require(!check(p, errors), "Unknown Struct field static diagnostic");
            ++passed;
        }
        {
            ExecutionSession session(Program{});
            session.evaluate("struct Stats:\n    health: Int\n");
            session.evaluate("let a = Stats(health: 42)\n");
            require(session.evaluate("a.health\n").returnValue.toString() == "42", "REPL persistent Struct");
            ++passed;
        }
        parity("fn main() -> Int:\n    let scores: Map<String, Int>\n    scores[\"JM\"] = 42\n    return "
               "scores[\"JM\"]\n",
               42);
        parity("fn scale(v: Vector2, factor: Float) -> Vector2:\n    return v * factor\nfn main() -> "
               "Float:\n    return scale(Vector2(8.0, 0.0), 0.5).x\n",
               4);
        if (LLVMBackend::available()) {
            NativeFunctionRegistry registry;
            NativeFunctionRegistry::Metadata info;
            info.symbol = "builtin.host.sum";
            info.parameterNames = {"values"};
            info.parameterTypes = {Type::List};
            info.parameterElementTypes = {Type::Int};
            info.returnType = Type::Int;
            registry.registerTypedFunction(info, [](const auto &args) {
                int64_t sum = 0;
                for (auto &item : *std::get<Value::ArrayPtr>(args[0].data))
                    sum += std::get<int64_t>(item.data);
                return Value(sum);
            });
            auto p = parse(
                "fn main() -> Int:\n    let values: List<Int> = [10, 20, 12]\n    return host.sum(values)\n");
            for (bool opt : {false, true})
                require(LLVMBackend{opt}.compile(lowerChecked(p, &registry), registry).invoke("main") == 42,
                        "Typed List FFI argument");
            ++passed;
            info.symbol = "builtin.host.values";
            info.parameterNames = {};
            info.parameterTypes = {};
            info.parameterElementTypes = {};
            info.returnType = Type::List;
            info.returnElementType = Type::Int;
            registry.registerTypedFunction(info, [](const auto &) { return Value::array({10, 20, 12}); });
            p = parse("fn main() -> Int:\n    let values = host.values()\n    return values[0] + values[1] + "
                      "values[2]\n");
            for (bool opt : {false, true})
                require(LLVMBackend{opt}.compile(lowerChecked(p, &registry), registry).invoke("main") == 42,
                        "Typed List FFI return");
            ++passed;
        } else
            ++skipped;
        parity("fn main() -> Float:\n    let 위치 = Vector2(4.0, 0.0)\n    return 위치.x\n", 4);
        parity("fn main() -> Int:\n    let names: Map<String, String> = {\"JM\": \"Engine\"}\n    "
               "names[\"JM\"] = \"42\"\n    return int(names[\"JM\"])\n",
               42);
        for (auto source : {"fn main() -> Int:\n    let v = Vector2(1.0, 0.0) / 0.0\n    return int(v.x)\n",
                            "fn main() -> Int:\n    let values: Map<String, Int> = {\"JM\": 42}\n    return "
                            "values[\"missing\"]\n",
                            "fn main() -> Int:\n    let total = 0\n    for i in range(0, 10, 0):\n        "
                            "total += i\n    return total\n"}) {
            auto p = parse(source);
            RunOptions options;
            options.entryFunction = "main";
            bool failed = false;
            try {
                execute(p, options);
            } catch (const std::exception &) {
                failed = true;
            }
            require(failed, "Interpreter invalid-data diagnostic");
            if (LLVMBackend::available())
                for (bool opt : {false, true}) {
                    failed = false;
                    try {
                        LLVMBackend{opt}.compile(lowerChecked(p)).invoke("main");
                    } catch (const std::exception &e) {
                        failed = std::string(e.what()).find("JM300") != std::string::npos;
                    }
                    require(failed, "Native invalid-data diagnostic");
                }
            ++passed;
        }
        if (LLVMBackend::available()) {
            auto p = parse("fn scale(v: Vector2, factor: Float) -> Vector2:\n    return v * factor\n");
            auto code = LLVMBackend{}.compile(lowerChecked(p));
            auto value = code.invokeValue("scale", {Value(Vector2Value{8, 0}), Value(0.5)});
            auto vector = std::get<Vector2Value>(value.data);
            require(vector.x == 4 && vector.y == 0, "Public Vector2 ABI round trip");
            ++passed;
        } else
            ++skipped;
        parity("fn scores() -> Map<String, Int>:\n    return {\"JM\": 42}\nfn lookup(values: Map<String, "
               "Int>) -> Int:\n    return values[\"JM\"]\nfn main() -> Int:\n    return lookup(scores())\n",
               42);
        {
            auto module = lowerChecked(parse(maps));
            bool changed = false;
            for (auto &function : module.functions)
                for (auto &block : function.blocks)
                    for (auto &instruction : block.instructions)
                        if (instruction.op == Op::RuntimeCall && instruction.immediate == JM_RT_MAP_CREATE) {
                            function.valueElementTypes.at(instruction.result) = Type::Float;
                            changed = true;
                        }
            LoweringDiagnostic diagnostic;
            require(changed && !verify(module, diagnostic), "Verifier rejects Map descriptor mismatch");
            ++passed;
        }
        memory();
        engine();
        aot();
        std::cout << "Native data regression: " << passed << " checks passed; " << skipped
                  << " unavailable configuration groups.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL native data after " << passed << " checks: " << e.what() << '\n';
        return 1;
    }
}
