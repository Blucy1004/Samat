#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/ModuleLoader.hpp"
#include "JMEngine/Script/RuntimeABI.h"
#if JMENGINE_HAS_ENGINE
#include "JMEngine/Script/EngineScriptAPI.hpp"
#endif
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#endif
using namespace jm::script;
using namespace jm::script::ir;
namespace {
size_t passed{};
void require(bool valid, const std::string &message) {
    if (!valid)
        throw std::runtime_error(message);
}
Program parse(std::string source) {
    Program result;
    Diagnostic diagnostic;
    require(parseCode(source, result, diagnostic), diagnostic.message);
    return result;
}
Module lowerChecked(const Program &program, const NativeFunctionRegistry *registry = nullptr) {
    Module module;
    LoweringDiagnostic diagnostic;
    require(lower(program, module, diagnostic, registry), diagnostic.message);
    return module;
}
void roundTrip(const Program &program) {
    Program korean, code;
    Diagnostic diagnostic;
    auto text = renderKorean(program);
    require(parseKorean(text, korean, diagnostic), diagnostic.message + "\n" + text);
    require(structurallyEqual(program, korean), "Korean AST mismatch");
    require(parseCode(renderCode(korean), code, diagnostic) && structurallyEqual(program, code),
            "Code AST mismatch");
}
void parity(const std::string &name, const std::string &source, const std::string &expected) {
    auto program = parse(source);
    roundTrip(program);
    RunOptions options;
    options.entryFunction = "main";
    require(execute(program, options).returnValue.toString() == expected, name + ": interpreter");
    auto module = lowerChecked(program);
    if (LLVMBackend::available())
        for (bool optimized : {false, true})
            require(LLVMBackend{optimized}.compile(module).invokeValue("main").toString() == expected,
                    name + ": LLVM");
    ++passed;
    std::cout << "PASS " << name << '\n';
}
void nativeFault(std::string source, std::string message) {
    auto module = lowerChecked(parse(source));
    if (LLVMBackend::available())
        for (bool optimized : {false, true}) {
            bool failed = false;
            try {
                LLVMBackend{optimized}.compile(module).invokeValue("main");
            } catch (const std::exception &error) {
                failed = std::string(error.what()).find(message) != std::string::npos;
            }
            require(failed, "Native fault diagnostic: " + message);
        }
    ++passed;
}
const std::string collections = R"(fn greet(name: String) -> String:
    return "Hello " + name
fn main() -> Int:
    let text = greet("사람")
    let parts = text.split(" ")
    let values = [5, 1, 8, 2, 3]
    values.sort()
    values[1] = 10
    values.push(4)
    let total = 0
    for value in values:
        total += value
    if text.contains("사람") && parts[1].codepointLength() == 2:
        return total + 11
    return 1
)";
void ffi() {
    if (!LLVMBackend::available()) {
        std::cout << "UNTESTED typed LLVM FFI: LLVM disabled\n";
        return;
    }
    NativeFunctionRegistry registry;
    registry.registerModule("host.test");
    registry.registerTypedFunction({"mix",
                                    "mix",
                                    "섞기",
                                    "Mixed Float/Int/Bool/String bridge",
                                    {"x", "count", "enabled", "label"},
                                    {Type::Float, Type::Int, Type::Bool, Type::String},
                                    Type::String},
                                   [](const std::vector<Value> &args) {
                                       require(std::get<double>(args[0].data) == 2.5 &&
                                                   std::get<int64_t>(args[1].data) == 4 &&
                                                   std::get<bool>(args[2].data),
                                               "FFI values");
                                       return Value(std::get<std::string>(args[3].data) + " ok");
                                   });
    auto program = parse("import host.test\nfn main() -> String:\n    return mix(x: 2.5, count: 4, enabled: "
                         "true, label: \"JM\")\n");
    auto module = lowerChecked(program, &registry);
    auto native = LLVMBackend{}.compile(module, registry);
    registry = NativeFunctionRegistry{};
    require(native.invokeValue("main").toString() == "JM ok", "Typed registry lifetime");
    ++passed;
    NativeFunctionRegistry bad;
    bad.registerTypedFunction({"bad", "bad", "bad", "", {}, {}, Type::Float},
                              [](const auto &) { return Value("wrong"); });
    auto malformed = lowerChecked(parse("fn main() -> Float:\n    return bad()\n"), &bad);
    bool failed = false;
    try {
        LLVMBackend{}.compile(malformed, bad).invokeValue("main");
    } catch (const std::exception &e) {
        failed = std::string(e.what()).find("JM7001") != std::string::npos;
    }
    require(failed, "FFI return type validation");
    ++passed;
    std::cout << "PASS typed FFI mixed signature, registry lifetime, invalid return\n";
}
void modules() {
    std::map<std::string, std::string> files{
        {"root", "import \"utils\"\nimport \"utils\"\nfn main() -> Int:\n    return helper()\n"},
        {"utils", "let count = 40\nfn helper() -> Int:\n    return count + 2\n"}};
    SourceResolver resolver = [&](std::string_view, std::string_view request) -> std::optional<ModuleSource> {
        auto found = files.find(std::string(request));
        if (found == files.end())
            return std::nullopt;
        return ModuleSource{found->first, found->second};
    };
    Program program;
    Diagnostic diagnostic;
    require(loadModules({"root", files["root"]}, resolver, program, diagnostic), diagnostic.message);
    RunOptions options;
    options.entryFunction = "main";
    require(execute(program, options).returnValue.toString() == "42", "Imported interpreter");
    if (LLVMBackend::available())
        require(LLVMBackend{}.compile(lowerChecked(program)).invoke("main") == 42, "Imported native");
    ++passed;
    roundTrip(parse(files["root"]));
    files["utils"] = "import \"root\"\n";
    require(!loadModules({"root", files["root"]}, resolver, program, diagnostic) &&
                diagnostic.message.find("Circular") != std::string::npos,
            "Import cycle");
    ++passed;
    files["utils"] = "fn main():\n    return 1\n";
    require(!loadModules({"root", files["root"]}, resolver, program, diagnostic) &&
                diagnostic.message.find("Duplicate") != std::string::npos,
            "Import duplicate symbol");
    ++passed;
    std::cout << "PASS file imports: once, shared AST, deterministic globals, cycles, duplicates\n";
}
void sessions() {
    auto program = parse("let count = 0\non start:\n    count += 10\non update:\n    count += 1\nfn main() "
                         "-> Int:\n    return count\n");
    ExecutionSession session(program);
    session.dispatch("start");
    session.dispatch("update");
    session.dispatch("update");
    require(session.invoke("main").returnValue.toString() == "12", "Persistent globals");
    ++passed;
#if JMENGINE_HAS_ENGINE
    const auto source = parse("import jm.game\nlet count = 0\non start:\n    count += 1\non "
                              "key.right.held:\n    player.moveFloat(direction: 1.0, speed: 8.5)\non "
                              "key.space.pressed:\n    player.jumpFloat(force: 12.5)\n");
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available())
            continue;
        jm::Scene scene;
        scene.create(jm::ObjectKind::Sprite2D).name = "Player";
        std::string id;
        float before = 0;
        for (auto &object : scene.objects())
            if (object.name == "Player") {
                id = object.id;
                before = object.position.x;
                object.grounded = true;
            }
        jm::EngineEventRuntime runtime(scene, id, source, backend);
        runtime.start();
        jm::ScriptInput input;
        input.rightHeld = true;
        runtime.tick(input, 0.25);
        runtime.tick(input, 0.25);
        input.rightHeld = false;
        input.spacePressed = true;
        runtime.tick(input, 0.25);
        for (auto &object : scene.objects())
            if (object.id == id) {
                require(std::abs(object.position.x - before - 4.25) < 1e-5, "Float engine movement");
                require(object.verticalVelocity == 12.5, "Float engine jump");
            }
        ++passed;
    }
#endif
#if JMENGINE_HAS_ENGINE
    auto sceneProgram = parse(
        "import jm.game\nlet created: String = \"\"\non start:\n    created = scene.createSprite(name: "
        "\"Extra\")\n    if scene.findId(name: \"Extra\") == created:\n        scene.destroy(id: created)\non "
        "update:\n    if input.isHeld(key: \"right\"):\n        transform.setPosition(x: 5.5, y: 3.5)\n      "
        "  physics.setVelocityY(velocity: 1.5)\n        physics.applyImpulseY(impulse: 2.0)\n");
    for (auto backend : {jm::EngineScriptBackend::Interpreter, jm::EngineScriptBackend::LLVM}) {
        if (backend == jm::EngineScriptBackend::LLVM && !LLVMBackend::available())
            continue;
        jm::Scene scene;
        auto id = scene.objects()[1].id;
        scene.objects()[1].mass = 1;
        auto count = scene.objects().size();
        jm::EngineEventRuntime runtime(scene, id, sceneProgram, backend);
        jm::ScriptInput input;
        input.rightHeld = true;
        runtime.tick(input, .25);
        require(scene.objects().size() == count, "Spawn/destroy Scene parity");
        require(scene.objects()[1].position.x == 5.5 && scene.objects()[1].position.y == 3.5 &&
                    scene.objects()[1].verticalVelocity == 3.5,
                "Transform/Physics/input parity");
        runtime.tick(input, .25);
        require(scene.objects().size() == count, "start dispatched only once");
        ++passed;
    }
#endif
    std::cout << "PASS persistent session and Float Scene event bridge\n";
}
void runtimeMemory() {
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto *context = jm_runtime_create_context();
        auto *previous = jm_runtime_activate(context);
        auto hello = jm_string_create("JM", 2),
             values = jm_runtime_call(JM_RT_LIST_CREATE, JM_RT_STRING, 0, 0);
        jm_runtime_call(JM_RT_LIST_PUSH, values, hello, JM_RT_STRING);
        auto copy = jm_runtime_call(JM_RT_LIST_CLONE, values, 0, 0);
        jm_runtime_call(JM_RT_LIST_CLEAR, values, 0, 0);
        require(jm_runtime_call(JM_RT_LENGTH, copy, 0, JM_RT_LIST) == 1, "Snapshot lifetime");
        bool failed = false;
        try {
            jm_runtime_call(JM_RT_LIST_GET, copy, 0, JM_RT_FLOAT);
        } catch (const std::exception &) {
            failed = true;
        }
        require(failed, "Runtime element metadata");
        jm_runtime_activate(previous);
        jm_runtime_destroy_context(context);
    }
    ++passed;
    std::cout << "PASS native runtime context destruction and element validation\n";
}
void aot() {
    if (!LLVMBackend::available())
        return;
#ifndef _WIN32
    auto directory =
        std::filesystem::temp_directory_path() /
        ("jm-plus-aot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{directory};
    auto executable = directory / "collections";
    LLVMBackend{true}.build(lowerChecked(parse(collections)), executable.string());
    int status = std::system(executable.c_str());
    require(WIFEXITED(status) && WEXITSTATUS(status) == 42, "Native String/List AOT actual exit");
    ++passed;
    auto invalid = directory / "bounds";
    LLVMBackend{}.build(
        lowerChecked(parse("fn main() -> Int:\n    let values = [1]\n    return values[4]\n")),
        invalid.string());
    status = std::system(invalid.c_str());
    require(WIFEXITED(status) && WEXITSTATUS(status) == 1, "AOT bounds failure");
    ++passed;
    std::cout << "PASS Linux String/List AOT exit 42 and bounds exit 1\n";
#endif
}
} // namespace
int main() {
    try {
        parity("native UTF-8 String/List algorithm", collections, "42");
        parity(
            "String methods",
            "fn main() -> String:\n    return \"  Hello 사람  \".trim().replace(\"Hello\", \"JM\").upper()\n",
            "JM 사람");
        parity("byte and codepoint lengths",
               "fn main() -> Int:\n    let text = \"사람\"\n    return text.length * 6 + "
               "text.codepointLength() * 3\n",
               "42");
        parity("mutable foreach snapshot",
               "fn main() -> Int:\n    let values = [10, 20, 12]\n    let total = 0\n    for value in "
               "values:\n        values.clear()\n        total += value\n    return total\n",
               "42");
        parity("Float list",
               "fn main() -> Float:\n    let values: List<Float> = [1.5, 2, 3.5]\n    values.insert(1, "
               "10.5)\n    values.reverse()\n    return values.removeAt(1) + values[1]\n",
               "12.5");
        parity("String globals and return",
               "let prefix: String = \"JM\"\nfn main() -> String:\n    prefix += \" Engine\"\n    return "
               "prefix\n",
               "JM Engine");
        parity("String numeric conversions",
               "fn main() -> Int:\n    return int(\"40\") + int(float(\"2.9\"))\n", "42");
        parity("exclusive range spelling",
               "fn main() -> Int:\n    let total = 0\n    for i in 0..<7:\n        total += i * 2\n    "
               "return total\n",
               "42");
        nativeFault("fn main() -> Int:\n    let values = [1]\n    return values[-1]\n", "JM3003");
        nativeFault("fn main() -> Int:\n    let values = [1]\n    values.clear()\n    return values.pop()\n",
                    "JM3004");
        nativeFault("fn main() -> Int:\n    return int(pow(2.0,63.0))\n", "JM3005");
        nativeFault("fn main() -> Int:\n    return 1 << -1\n", "JM3004");
        parity("typed List function and result",
               "fn scale(values: List<Float>) -> List<Float>:\n    values.push(4)\n    return values\nfn "
               "main() -> List<Float>:\n    let values: List<Float> = [1, 2.5]\n    return scale(values)\n",
               "[1, 2.5, 4]");
        parity("typed empty List",
               "fn main() -> Int:\n    let words: List<String>\n    words.push(\"JM\")\n    return "
               "words[0].length * 21\n",
               "42");
        parity("extended math", "fn main() -> Float:\n    return trunc(41.8) + smoothstep(0, 1, 0.5) * 2\n",
               "42");
        parity("deterministic random",
               "fn main() -> Int:\n    seed(123)\n    let first = randomInt(-100,100)\n    seed(123)\n    if "
               "first == randomInt(-100,100):\n        return 42\n    return 1\n",
               "42");
        {
            auto program = parse("fn main() -> Float:\n    let a: Vector2 = Vector2(3,4)\n    let b: Vector3 "
                                 "= Vector3(1,0,0).cross(Vector3(0,1,0))\n    let red: Color = Color.red\n   "
                                 " return a.length() + b.z + red.r\n");
            roundTrip(program);
            RunOptions options;
            options.entryFunction = "main";
            require(execute(program, options).returnValue.toString() == "7", "Vector2/3/Color interpreter");
            ++passed;
        }
        {
            ExecutionSession repl(Program{});
            repl.evaluate("let values: List<Int> = [1]\n");
            bool failed = false;
            try {
                repl.evaluate("values.push(2.5)\n");
            } catch (const std::exception &error) {
                failed = std::string(error.what()).find("Typed List") != std::string::npos;
            }
            require(failed, "Typed alias mutation");
            repl.evaluate("let x = 40\n");
            repl.evaluate("x += 2\n");
            require(repl.evaluate("x\n").returnValue.toString() == "42", "REPL persistent state");
            ++passed;
        }
        {
            auto program = parse(
                "enum Direction:\n    left\n    right\nstruct Stats:\n    health: Int\n    speed: Float\n    "
                "name: String = \"Player\"\nfn main() -> Int:\n    let stats = Stats(speed: 8.5, health: "
                "100)\n    let alias = stats\n    alias.health -= 58\n    let direction = Direction.right\n  "
                "  if direction == Direction.right:\n        return stats.health\n    return 1\n");
            roundTrip(program);
            RunOptions options;
            options.entryFunction = "main";
            require(execute(program, options).returnValue.toString() == "42",
                    "Enum/Struct shared references");
            Module module;
            LoweringDiagnostic diagnostic;
            require(!lower(program, module, diagnostic), "Unsupported native data declarations accepted");
            ++passed;
        }
        {
            auto program = parse(
                "fn main() -> Int:\n    let position: Tuple = (10, 20, 12)\n    let total = 0\n    for i in "
                "range(6, 0, -2):\n        total += i\n    return position.0 + position[1] + total\n");
            roundTrip(program);
            RunOptions options;
            options.entryFunction = "main";
            require(execute(program, options).returnValue.toString() == "42", "Tuple and descending Range");
            ++passed;
        }
        {
            auto program = parse("struct Node:\n    child: Any\nfn main() -> Int:\n    let node = "
                                 "Node(child: null)\n    node.child = node\n    return 0\n");
            RunOptions options;
            options.entryFunction = "main";
            bool failed = false;
            try {
                execute(program, options);
            } catch (const std::exception &error) {
                failed = std::string(error.what()).find("JM3010") != std::string::npos;
            }
            require(failed, "Struct cycle rejection");
            ++passed;
        }
        {
            std::mt19937 random(505);
            for (int iteration = 0; iteration < 32; ++iteration) {
                std::string literal = "[";
                int64_t expected = 0;
                for (int i = 0; i < 8; ++i) {
                    auto value = static_cast<int>(random() % 101) - 50;
                    if (i)
                        literal += ", ";
                    literal += std::to_string(value);
                    expected += value;
                }
                literal += "]";
                parity("seeded collection differential " + std::to_string(iteration),
                       "fn main() -> Int:\n    let values: List<Int> = " + literal +
                           "\n    values.sort()\n    values.reverse()\n    values.insert(0, 42)\n    "
                           "values.removeAt(0)\n    let total = 0\n    for value in values:\n        total "
                           "+= value\n    return total\n",
                       std::to_string(expected));
            }
        }
        modules();
        ffi();
        sessions();
        runtimeMemory();
        aot();
        std::cout << "Extended regression: " << passed << " checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL extended: " << error.what() << '\n';
        return 1;
    }
}
