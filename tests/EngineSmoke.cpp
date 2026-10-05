#include "JMEngine/Project/ProjectStore.hpp"
#include "JMEngine/Renderer/Viewport.hpp"
#include "JMEngine/Script/KoreanParticles.hpp"
#include "JMEngine/Script/Script.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/JMIR.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class TemporaryProject {
public:
    TemporaryProject() {
        const jm::ProjectSettings seed = jm::ProjectStore::makeNewProject("Smoke Project");
        path = std::filesystem::temp_directory_path() / ("JMEngineSmoke-" + seed.projectId);
    }
    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

bool sameSemantics(const jm::ScriptDocument& left, const jm::ScriptDocument& right) {
    if (left.variables.size() != right.variables.size() || left.functions.size() != right.functions.size()) return false;
    for (std::size_t i = 0; i < left.variables.size(); ++i) {
        if (left.variables[i].name != right.variables[i].name || left.variables[i].constant != right.variables[i].constant ||
            std::abs(left.variables[i].value - right.variables[i].value) > 0.0001F) return false;
    }
    for (std::size_t i = 0; i < left.functions.size(); ++i) {
        const auto& a = left.functions[i];
        const auto& b = right.functions[i];
        if (a.name != b.name || a.parameters != b.parameters || a.body.size() != b.body.size()) return false;
        for (std::size_t j = 0; j < a.body.size(); ++j) {
            const auto& x = a.body[j];
            const auto& y = b.body[j];
            if (x.action != y.action || x.targetObjectId != y.targetObjectId || x.functionName != y.functionName || x.functionArguments != y.functionArguments ||
                x.valueVariable != y.valueVariable || (x.valueVariable.empty() && std::abs(x.value - y.value) > 0.0001F)) return false;
        }
    }
    if (left.handlers.size() != right.handlers.size()) return false;
    for (std::size_t i = 0; i < left.handlers.size(); ++i) {
        const auto& a = left.handlers[i];
        const auto& b = right.handlers[i];
        if (a.event != b.event || a.body.size() != b.body.size()) return false;
        for (std::size_t j = 0; j < a.body.size(); ++j) {
            const auto& x = a.body[j];
            const auto& y = b.body[j];
            if (x.action != y.action || x.targetObjectId != y.targetObjectId || x.functionName != y.functionName || x.functionArguments != y.functionArguments ||
                x.valueVariable != y.valueVariable || (x.valueVariable.empty() && std::abs(x.value - y.value) > 0.0001F))
                return false;
        }
    }
    return true;
}

void checkViewportScaling() {
    const jm::PixelViewport viewport = jm::scaleViewportToPixels(
        250.0F, 94.0F, 450.0F, 500.0F, 1000.0F, 700.0F, 2000, 1400);
    require(viewport.x == 500 && viewport.yFromTop == 188 && viewport.width == 900 && viewport.height == 1000,
            "Viewport coordinates must scale with the framebuffer.");
    require(jm::viewportBottomLeftY(viewport, 1400) == 212,
            "Top-left editor coordinates must convert to the OpenGL viewport origin.");
    const jm::PixelViewport clipped = jm::scaleViewportToPixels(
        900.0F, 650.0F, 400.0F, 200.0F, 1000.0F, 700.0F, 1000, 700);
    require(clipped.x + clipped.width == 1000 && clipped.yFromTop + clipped.height == 700,
            "Viewport bounds must remain inside the resized window.");
}

void checkParticles() {
    using jm::KoreanParticle;
    require(jm::attachKoreanParticle("플레이어", KoreanParticle::Subject) == "플레이어가", "Vowel ending selects 가.");
    require(jm::attachKoreanParticle("적", KoreanParticle::Subject) == "적이", "Consonant ending selects 이.");
    require(jm::attachKoreanParticle("플레이어", KoreanParticle::Object) == "플레이어를", "Vowel ending selects 를.");
    require(jm::attachKoreanParticle("길", KoreanParticle::Direction) == "길로", "Rieul ending uses 로.");
    require(jm::attachKoreanParticle("바닥", KoreanParticle::Direction) == "바닥으로", "Consonant ending uses 으로.");
}

void checkLanguageCore() {
    using namespace jm::script;
    auto run = [](const std::string& source) {
        Program program; Diagnostic diagnostic;
        if (!parseCode(source, program, diagnostic)) throw std::runtime_error("Language program parse failed: " + diagnostic.message + "\n" + source);
        return execute(program);
    };
    auto value = [](const ExecutionResult& result, const char* name) { return result.globals->get(name); };

    const auto whileResult = run("let x = 0\nwhile x < 100:\n    x += 1\nassert(x == 100)\n");
    require(value(whileResult, "x").toString() == "100", "Runtime while loop must mutate variables until its condition becomes false.");
    const auto scopes = run("let visible = 7\nlet n = 17\nlet sum = 0\nwhile n > 0:\n    sum += n\n    n -= 1\n"
                            "if true && !false or false:\n    let visible = 99\nassert(visible == 7)\nassert(sum == 153)\n");
    require(value(scopes, "sum").toString() == "153", "while bounds must depend on runtime values and lexical blocks must keep shadowed names local.");
    const auto forLoops = run("let total = 0\nfor i in 0..5:\n    total += i\n"
                              "for item in [2, 3, 4]:\n    total += item\nassert(total == 19)\n");
    require(value(forLoops, "total").toString() == "19", "Range and collection for-loops must execute in the language core.");
    Program inputLoop; Diagnostic inputDiagnostic;
    require(parseCode("let result = 0\nwhile n > 0:\n    result += n\n    n -= 1\n", inputLoop, inputDiagnostic),
            "Input-driven loop program must parse.");
    RunOptions inputOptions; inputOptions.initialValues.emplace("n", Value(17));
    const auto inputSum = execute(inputLoop, inputOptions);
    require(value(inputSum, "result").toString() == "153", "The host must be able to inject runtime input values without an engine API.");

    const auto recursive = run(
        "fn factorial(n):\n    if n <= 1:\n        return 1\n    return n * factorial(n - 1)\n"
        "fn fibonacci(n):\n    if n <= 1:\n        return n\n    return fibonacci(n - 1) + fibonacci(n - 2)\n"
        "fn gcd(a, b):\n    if b != 0:\n        return gcd(b, a % b)\n    return a\n"
        "let fact = factorial(5)\nlet fib = fibonacci(10)\nlet divisor = gcd(48, 18)\n"
        "assert(fact == 120)\nassert(fib == 55)\nassert(divisor == 6)\n");
    require(value(recursive, "fact").toString() == "120" && value(recursive, "fib").toString() == "55" &&
            value(recursive, "divisor").toString() == "6", "Recursive functions must return values through nested calls.");

    const auto collections = run("let values = [5, 2, 9, 1, 7]\nvalues.append(4)\nvalues[1] = 8\n"
                                 "let data = {score: 10}\ndata.score = values[0] + values.length\n"
                                 "assert(values[1] == 8)\nassert(len(values) == 6)\nassert(data.score == 11)\n");
    (void)collections;

    const auto sorted = run(
        "let numbers = [5, 3, 8, 1, 2]\nlet i = 0\nwhile i < numbers.length:\n    let j = 0\n"
        "    while j < numbers.length - i - 1:\n        if numbers[j] > numbers[j + 1]:\n"
        "            let temp = numbers[j]\n            numbers[j] = numbers[j + 1]\n            numbers[j + 1] = temp\n"
        "        j += 1\n    i += 1\n"
        "assert(numbers == [1, 2, 3, 5, 8])\n");
    (void)sorted;

    const auto fizzBuzz = run(
        "let i = 1\nlet outputs = []\nwhile i <= 100:\n    if i % 15 == 0:\n        outputs.append(\"FizzBuzz\")\n"
        "    else:\n        if i % 3 == 0:\n            outputs.append(\"Fizz\")\n        else:\n"
        "            if i % 5 == 0:\n                outputs.append(\"Buzz\")\n            else:\n                outputs.append(i)\n"
        "    print(outputs[outputs.length - 1])\n    i += 1\nassert(outputs.length == 100)\nassert(outputs[14] == \"FizzBuzz\")\n"
        "assert(outputs[2] == \"Fizz\")\nassert(outputs[4] == \"Buzz\")\n");
    require(fizzBuzz.output.size() == 100 && fizzBuzz.output[14] == "FizzBuzz" && fizzBuzz.output[2] == "Fizz" &&
            fizzBuzz.output[4] == "Buzz", "FizzBuzz must execute and emit its expected values through the pure runtime.");

    const std::string code = "let x = 0\nwhile x < 100:\n    x += 1\n";
    const std::string korean = "숫자 변수 x를 0으로 정한다.\nx가 100보다 작은 동안:\n    x를 1만큼 늘린다.\n";
    Program codeAst, koreanAst; Diagnostic diagnostic;
    require(parseCode(code, codeAst, diagnostic), "Code syntax sample must parse.");
    if (!parseKorean(korean, koreanAst, diagnostic)) throw std::runtime_error("Korean syntax parse failed: " + diagnostic.message);
    require(structurallyEqual(codeAst, koreanAst), "Korean and Code syntax must produce structurally identical JM ASTs.");
    const auto koreanResult = execute(koreanAst);
    require(value(koreanResult, "x").toString() == "100", "Korean syntax must execute through the shared language runtime.");
    const std::string koreanFactorial = "함수 factorial(n):\n    n이 1보다 작거나 같다면:\n        1을 반환한다.\n"
                                       "    n * factorial(n - 1)을 반환한다.\n숫자 변수 result를 factorial(5)로 정한다.\n";
    Program koreanFunctionAst;
    if (!parseKorean(koreanFactorial, koreanFunctionAst, diagnostic)) throw std::runtime_error("Korean function parse failed: " + diagnostic.message);
    const auto koreanFunctionResult = execute(koreanFunctionAst);
    require(value(koreanFunctionResult, "result").toString() == "120", "Korean functions, returns, expressions, and recursion must use the shared runtime.");

    RunOptions bounded; bounded.instructionBudget = 100;
    Program runaway;
    require(parseCode("while true:\n    print(1)\n", runaway, diagnostic), "An intentional infinite loop must remain valid language syntax.");
    bool stopped = false;
    try { (void)execute(runaway, bounded); } catch (const std::runtime_error& error) { stopped = std::string(error.what()).find("instruction budget") != std::string::npos; }
    require(stopped, "The runtime instruction budget must safely stop runaway code.");
    RunOptions cancelled; cancelled.shouldStop = [] { return true; };
    bool cancellationObserved = false;
    try { (void)execute(runaway, cancelled); } catch (const std::runtime_error& error) { cancellationObserved = std::string(error.what()).find("stopped by the editor") != std::string::npos; }
    require(cancellationObserved, "A host cancellation callback must be able to stop a running program.");
    bounded.instructionBudget = 10000; bounded.recursionLimit = 12;
    Program recursion;
    require(parseCode("fn forever():\n    return forever()\nforever()\n", recursion, diagnostic), "Recursive code must remain syntactically valid.");
    bool recursionStopped = false;
    try { (void)execute(recursion, bounded); } catch (const std::runtime_error& error) { recursionStopped = std::string(error.what()).find("recursion limit") != std::string::npos; }
    require(recursionStopped, "The runtime must report recursive runaway scripts without crashing the editor.");
}

void checkNativeBackend() {
    using namespace jm::script;
    using namespace jm::script::ir;
    const std::string source =
        "fn factorial(n):\n"
        "    if n <= 1:\n"
        "        return 1\n"
        "    return n * factorial(n - 1)\n"
        "fn fibonacci(n):\n"
        "    if n <= 1:\n"
        "        return n\n"
        "    return fibonacci(n - 1) + fibonacci(n - 2)\n"
        "fn gcd(a, b):\n"
        "    if b != 0:\n"
        "        return gcd(b, a % b)\n"
        "    return a\n"
        "fn return42():\n"
        "    return 42\n"
        "fn elsePath():\n"
        "    if false:\n"
        "        return 1\n"
        "    else:\n"
        "        return 2\n"
        "fn booleanPath():\n"
        "    if true and !false:\n"
        "        return 1\n"
        "    return 0\n"
        "fn shortCircuit():\n"
        "    if false and 1 / 0 == 0:\n"
        "        return 1\n"
        "    return 2\n"
        "fn main():\n"
        "    let x = 0\n"
        "    while x < 5:\n"
        "        x += 1\n"
        "    if x == 5:\n"
        "        return factorial(x) + 2 * 3\n"
        "    else:\n"
        "        return 0\n";
    Program program; Diagnostic parseDiagnostic;
    require(parseCode(source,program,parseDiagnostic),"Native sample source must parse.");
    Program interpreterProgram; Diagnostic interpreterDiagnostic;
    require(parseCode(source+"let result = main()\n",interpreterProgram,interpreterDiagnostic),"Interpreter comparison source must parse.");
    const auto interpreted=execute(interpreterProgram);
    require(interpreted.globals->get("result").toString()=="126","Interpreter reference result must be stable.");
    Module module; LoweringDiagnostic loweringDiagnostic;
    require(lower(program,module,loweringDiagnostic),("AST must lower to JM IR: "+loweringDiagnostic.message).c_str());
    const std::string irText=format(module);
    require(irText.find("br_if")!=std::string::npos && irText.find("call @factorial")!=std::string::npos,
            "JM IR should expose control flow and calls.");
    X64Backend backend;
    NativeCode native=backend.compile(module);
    require(!native.machineCode("main").empty(),"x86-64 backend must emit machine code.");
    const auto interpretedValue=std::stoll(interpreted.globals->get("result").toString());
    const auto nativeValue=native.invoke("main");
    require(nativeValue==interpretedValue,("Native result "+std::to_string(nativeValue)+" must match interpreter "+std::to_string(interpretedValue)+".").c_str());
    require(native.invoke("factorial",{5})==120,"Native function parameters and recursion must execute.");
    require(native.invoke("fibonacci",{10})==55,"Native Fibonacci recursion must execute.");
    require(native.invoke("gcd",{48,18})==6,"Native modulo and recursive Euclidean GCD must execute.");
    require(native.invoke("return42")==42,"Native constant return must execute.");
    require(native.invoke("elsePath")==2 && native.invoke("booleanPath")==1,"Native else blocks and boolean operators must execute.");
    require(native.invoke("shortCircuit")==2,"Native boolean operators must preserve interpreter short-circuit behavior.");

    Program korean; Diagnostic koreanDiagnostic;
    require(parseKorean("함수 main():\n    42를 반환한다\n",korean,koreanDiagnostic),"Korean native syntax sample must parse.");
    Module koreanModule; require(lower(korean,koreanModule,loweringDiagnostic),"Korean AST must lower through the same JM IR.");
    NativeCode koreanNative=backend.compile(koreanModule);
    require(koreanNative.invoke("main")==42,"Korean parser output must run as native x86-64 code.");

    Program ffiProgram; require(parseCode("fn main():\n    return player.jump(12)\n",ffiProgram,parseDiagnostic),"Stable builtin member syntax must parse.");
    Module ffiModule; require(lower(ffiProgram,ffiModule,loweringDiagnostic),"Stable builtin call must lower to the shared IR.");
    require(format(ffiModule).find("builtin.player.jump")!=std::string::npos,"Engine interop must use a stable builtin.* symbol in JM IR.");
    NativeFunctionRegistry registry;
    registry.registerFunction("builtin.player.jump",[](std::int64_t force,std::int64_t,std::int64_t,std::int64_t){return force+100;});
    NativeCode ffiNative=backend.compile(ffiModule,registry);
    require(ffiNative.invoke("main")==112,"Native builtin registry must cross the C++ FFI boundary.");
}

void checkScriptRuntimeAndProjectFlow() {
    jm::Scene scene;
    jm::GameObject* player = nullptr;
    for (jm::GameObject& object : scene.objects()) {
        if (object.name == "Player") player = &object;
    }
    require(player != nullptr && player->physicsEnabled, "Default scene should contain a physics-enabled Player.");
    jm::ScriptDocument original = jm::makeDefaultScript(scene);
    require(original.handlers.size() == 4, "Starter script should contain start, left, right, and jump events.");

    const std::string code = jm::formatScriptCode(original, scene);
    require(code.find("let moveSpeed = 8") != std::string::npos && code.find("fn moveRight():") != std::string::npos &&
            code.find("on start:") != std::string::npos && code.find("setupPlayer(moveSpeed)") != std::string::npos &&
            code.find("fn setupPlayer(speed):") != std::string::npos,
            "Code view should expose a variable, reusable functions, and events.");
    require(code.find("Player.move(direction: right)") != std::string::npos &&
            code.find("Player.jump(force: 12)") != std::string::npos,
            "Code view should render movement and jump events.");
    jm::ScriptDiagnostic diagnostic;
    jm::ScriptDocument codeParsed = original;
    require(jm::parseScriptCode(code, scene, codeParsed, diagnostic), "Formatted code should parse back.");
    require(sameSemantics(original, codeParsed), "Code syntax round-trip must preserve AST semantics.");

    const std::string korean = jm::formatScriptKorean(original, scene);
    require(korean.find("플레이어가 땅에 닿아 있다면") != std::string::npos,
            "Korean renderer should use the correct subject particle.");
    jm::ScriptDocument koreanParsed = original;
    require(jm::parseScriptKorean(korean, scene, koreanParsed, diagnostic), "Formatted Korean syntax should parse back.");
    require(sameSemantics(original, koreanParsed), "Korean syntax round-trip must preserve AST semantics.");
    jm::ScriptDocument koreanToCode = koreanParsed;
    require(jm::parseScriptCode(jm::formatScriptCode(koreanParsed, scene), scene, koreanToCode, diagnostic) &&
            sameSemantics(koreanParsed, koreanToCode), "Korean to AST to Code to AST must preserve meaning.");
    jm::ScriptDocument codeToKorean = codeParsed;
    require(jm::parseScriptKorean(jm::formatScriptKorean(codeParsed, scene), scene, codeToKorean, diagnostic) &&
            sameSemantics(codeParsed, codeToKorean), "Code to AST to Korean to AST must preserve meaning.");
    jm::ScriptDocument invalid = original;
    require(!jm::parseScriptCode("on start:\n    Player.speed = 999999999999999999999999999999999999999\n", scene, invalid, diagnostic) &&
            !diagnostic.message.empty(), "Malformed numeric input should produce a friendly diagnostic, not throw.");

    const std::string userCode = "let runSpeed = 3\nconst boost = 2\n\nfn prepare(speed):\n    Player.speed = speed * boost\n\nfn goRight():\n    Player.move(direction: right)\n\non start:\n    prepare(runSpeed + 1)\n\non key.right.held:\n    goRight()\n";
    jm::ScriptDocument userScript;
    require(jm::parseScriptCode(userCode, scene, userScript, diagnostic), "User-authored variables and functions should parse.");
    jm::ScriptProgram userProgram;
    require(jm::compileScript(userScript, scene, userProgram, diagnostic), "User-authored functions should compile and resolve variables.");
    jm::executeScriptStart(userProgram, scene);
    require(std::abs(player->movementSpeed - 8.0F) < 0.001F, "Parameterized functions should evaluate arguments and arithmetic with global constants.");
    const std::string userKorean = jm::formatScriptKorean(userScript, scene);
    jm::ScriptDocument userKoreanParsed;
    require(jm::parseScriptKorean(userKorean, scene, userKoreanParsed, diagnostic) && sameSemantics(userScript, userKoreanParsed),
            "Korean syntax must round-trip function parameters and calls over the shared script model.");
    jm::ScriptDocument badCall;
    require(jm::parseScriptCode("on start:\n    missingFunction()\n", scene, badCall, diagnostic), "Function call names are resolved at compile time.");
    jm::ScriptProgram badProgram;
    require(!jm::compileScript(badCall, scene, badProgram, diagnostic), "Calling an unknown function should fail compilation clearly.");
    jm::ScriptDocument wrongArity;
    require(jm::parseScriptCode("fn setSpeed(value):\n    Player.speed = value\n\non start:\n    setSpeed()\n", scene, wrongArity, diagnostic),
            "A function with a missing call argument should remain editable until compile diagnostics.");
    require(!jm::compileScript(wrongArity, scene, badProgram, diagnostic) && diagnostic.message.find("인자 수") != std::string::npos,
            "Function argument count mismatch should give a beginner-friendly compiler diagnostic.");
    jm::ScriptDocument unknownValue;
    require(jm::parseScriptCode("on start:\n    Player.speed = missingSpeed\n", scene, unknownValue, diagnostic) &&
            !jm::compileScript(unknownValue, scene, badProgram, diagnostic),
            "Unresolved values should be caught during compilation rather than silently becoming zero.");

    jm::ScriptProgram program;
    require(jm::compileScript(koreanParsed, scene, program, diagnostic), "Valid script should compile to runtime instructions.");
    jm::executeScriptStart(program, scene);
    require(std::abs(player->movementSpeed - 8.0F) < 0.001F, "Start event should initialize movement speed.");
    constexpr float step = 1.0F / 60.0F;
    const float initialX = player->position.x;
    for (int tick = 0; tick < 60; ++tick) {
        jm::executeScript(program, scene, jm::ScriptInput{true, false, false}, step);
        scene.stepPhysics2D(step);
    }
    require(std::abs(player->position.x - (initialX + 8.0F)) < 0.01F,
            "One second of held right input should move at the configured speed.");
    require(player->grounded, "Falling Player should collide with and rest on the floor.");
    jm::executeScript(program, scene, jm::ScriptInput{false, false, true}, step);
    require(player->verticalVelocity > 0.0F && !player->grounded, "Space event should jump only from the ground.");
    const float groundedY = player->position.y;
    scene.stepPhysics2D(step);
    require(player->position.y > groundedY, "Jump should move the Player upward.");

    jm::ProjectSettings settings = jm::ProjectStore::makeNewProject("Smoke Project");
    TemporaryProject temporary;
    jm::ProjectStore::save(temporary.path, settings, scene, koreanParsed);
    jm::ProjectDocument loaded = jm::ProjectStore::load(temporary.path);
    require(loaded.settings.projectId == settings.projectId, "Project identity must survive save and load.");
    require(loaded.scene.objects().size() == scene.objects().size(), "Scene object count must survive save and load.");
    require(sameSemantics(loaded.script, koreanParsed), "All script events and actions must survive save and load.");
    const jm::GameObject* loadedPlayer = nullptr;
    for (const jm::GameObject& object : loaded.scene.objects()) if (object.id == player->id) loadedPlayer = &object;
    require(loadedPlayer != nullptr && loadedPlayer->physicsEnabled && std::abs(loadedPlayer->mass - player->mass) < 0.001F,
            "Physics component settings must survive save and load.");
    require(loadedPlayer != nullptr && loadedPlayer->layer == player->layer && loadedPlayer->visible == player->visible,
            "Object layer and visibility must survive save and load.");
}

} // namespace

int main() {
    try {
        checkViewportScaling();
        checkParticles();
        checkLanguageCore();
        checkNativeBackend();
        checkScriptRuntimeAndProjectFlow();
    } catch (const std::exception& exception) {
        std::cerr << "JM Engine vertical-slice simulation failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "JM Engine viewport, Korean particles, dual-syntax AST, runtime physics, and project persistence passed.\n";
}
