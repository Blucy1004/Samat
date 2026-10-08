#include "JMEngine/Script/EngineScriptAPI.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string sourceFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + path.string());
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    std::string normalized;
    normalized.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r' && i + 1 < source.size() && source[i + 1] == '\n')
            continue;
        normalized.push_back(source[i]);
    }
    return normalized;
}
jm::script::Program parse(const std::string &source) {
    jm::script::Program program;
    jm::script::Diagnostic diagnostic;
    require(jm::script::parseCode(source, program, diagnostic), diagnostic.message);
    return program;
}
jm::GameObject &object(jm::Scene &scene, std::string_view name) {
    auto found = std::find_if(scene.objects().begin(), scene.objects().end(),
                              [&](const auto &item) { return item.name == name; });
    if (found == scene.objects().end())
        throw std::runtime_error("Missing spawned object: " + std::string(name));
    return *found;
}
double number(const std::map<std::string, std::string> &snapshot, std::string_view name) {
    auto found = snapshot.find(std::string(name));
    if (found == snapshot.end())
        throw std::runtime_error("Missing inspected game value: " + std::string(name));
    auto colon = found->second.find(": ");
    return std::stod(found->second.substr(colon + 2));
}
std::string editGlobal(std::string source, std::string_view before, std::string_view after) {
    auto position = source.find(before);
    require(position != std::string::npos, "Game source edit anchor not found.");
    source.replace(position, before.size(), after);
    return source;
}
void runBackend(const std::string &source, jm::EngineScriptBackend backend) {
    jm::Scene scene;
    auto originalCount = scene.objects().size();
    const auto playerId = scene.objects()[1].id;
    jm::EngineEventRuntime runtime(scene, playerId, parse(source), backend);
    runtime.start();
    require(scene.objects().size() == originalCount + 3, "Samat did not spawn three Pong sprites.");
    auto &left = object(scene, "Left Paddle");
    auto &right = object(scene, "Right Paddle");
    auto &ball = object(scene, "Ball");
    require(left.kind == jm::ObjectKind::Sprite2D && right.kind == jm::ObjectKind::Sprite2D &&
                ball.kind == jm::ObjectKind::Sprite2D,
            "Pong entities are not renderable Sprite2D objects.");
    require(std::abs(left.scale.x - 0.35F) < 1e-5 && std::abs(left.scale.y - 2.2F) < 1e-5 &&
                left.color.x != right.color.x,
            "Samat transform/render properties did not reach spawned objects.");
    require(std::abs(std::abs(number(runtime.inspect(), "ballVelocityX")) - 6.0) < 1e-5,
            "Seeded serve did not produce a horizontal ball direction.");

    jm::ScriptInput input;
    input.keysHeld = {"w", "up"};
    runtime.tick(input, 0.1);
    require(std::abs(object(scene, "Left Paddle").position.y - 0.8F) < 1e-5 &&
                std::abs(object(scene, "Right Paddle").position.y - 0.8F) < 1e-5,
            "Keyboard input did not move both paddles.");

    auto faster = editGlobal(source, "let paddleSpeed: Float = 8.0", "let paddleSpeed: Float = 20.0");
    jm::script::Diagnostic diagnostic;
    require(runtime.hotSwap(parse(faster), diagnostic),
            "Paddle speed live edit failed: " + diagnostic.message);
    runtime.tick(input, 0.1);
    require(std::abs(object(scene, "Left Paddle").position.y - 2.8F) < 1e-5,
            "Live paddleSpeed edit did not affect the running game.");

    // Run a fresh game with a deterministic serve aimed at the left paddle.
    auto bounce = editGlobal(source, "let ballVelocityY: Float = 0.0", "let ballVelocityY: Float = 0.0\nlet testBounce = true");
    bounce = editGlobal(bounce, "    if randomInt(0, 2) == 0:", "    if testBounce:\n        ballX = -7.3\n        ballY = 0.0\n        ballVelocityX = -4.0\n        ballVelocityY = 0.0\n    else if randomInt(0, 2) == 0:");
    jm::Scene collisionScene;
    jm::EngineEventRuntime collisionRuntime(collisionScene, {}, parse(bounce));
    collisionRuntime.start();
    collisionRuntime.tick({}, 0.0);
    require(number(collisionRuntime.inspect(), "ballVelocityX") > 0.0,
            "Samat paddle overlap did not reflect the ball.");

    auto goal = bounce;
    goal = editGlobal(goal, "        ballVelocityX = -4.0", "        ballVelocityX = -4.0");
    goal = editGlobal(goal, "if ballX < -8.5:", "if ballX < -8.7:");
    goal = editGlobal(goal, "    serve()\n    println(", "    serve()\n    ballX = -9.0\n    println(");
    jm::Scene goalScene;
    jm::EngineEventRuntime goalRuntime(goalScene, {}, parse(goal));
    goalRuntime.start();
    goalRuntime.tick({}, 0.0);
    auto goalState = goalRuntime.inspect();
    require(number(goalState, "rightScore") == 1.0 && number(goalState, "ballX") == -7.3,
            "Goal did not update score and reset the ball: score=" + goalState.at("rightScore") +
                " ball=" + goalState.at("ballX"));
    input.keysPressed = {"space"};
    goalRuntime.tick(input, 0.0);
    require(number(goalRuntime.inspect(), "rightScore") == 0.0, "Space input did not reset the score.");
}
} // namespace

int main() {
    try {
        const std::filesystem::path root = JM_SOURCE_DIR;
        const std::string genericKeySource = R"ST(let pressed: Int = 0
on key.escape.pressed:
    pressed += 1
on update:
    if input.wasPressed("escape") and input.isHeld("escape"):
        pressed += 1
)ST";
        jm::Scene keyScene;
        jm::EngineEventRuntime keyRuntime(keyScene, {}, parse(genericKeySource));
        keyRuntime.start();
        jm::ScriptInput escapeInput;
        escapeInput.keysPressed = {"escape"};
        escapeInput.keysHeld = {"escape"};
        keyRuntime.tick(escapeInput, 0.0);
        require(number(keyRuntime.inspect(), "pressed") == 2.0,
                "Named Escape key event/input did not match the smart key picker spelling.");

        const auto source = sourceFile(root / "examples/Samat/pong.st");
        const auto code = parse(source);
        auto registry = jm::engineNativeFunctions();
        std::vector<jm::script::Diagnostic> diagnostics;
        require(jm::script::check(code, diagnostics, &registry),
                diagnostics.empty() ? "Pong type check" : diagnostics.front().message);

        const char *snippetSource = R"ST(import jm.game
on start:
    let sprite = scene.spawn("Parity", Vector2(0, 0), Vector2(1, 1), Color(1, 1, 1))
    if sprite != null:
        sprite.setPosition(Vector2(2, 3))
)ST";
        auto snippet = parse(snippetSource);
        jm::script::Program korean;
        jm::script::Diagnostic diagnostic;
        auto koreanSource = jm::script::renderKorean(snippet);
        require(jm::script::parseKorean(koreanSource, korean, diagnostic), diagnostic.message);
        require(jm::script::structurallyEqual(snippet, korean),
                "Korean gameplay API produced a different AST.");

        runBackend(source, jm::EngineScriptBackend::Interpreter);
        if (jm::script::ir::LLVMBackend::available())
            runBackend(source, jm::EngineScriptBackend::LLVM);
        std::cout << "Samat Pong: direct .st gameplay, Code/Korean AST, input, rendering transforms, "
                     "collision, score, reset, and live edit PASS.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Samat gameplay regression failed: " << error.what() << '\n';
        return 1;
    }
}
