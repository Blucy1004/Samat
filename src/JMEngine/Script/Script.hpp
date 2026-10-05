#pragma once

#include "JMEngine/Scene/Scene.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace jm {

enum class ScriptEvent {
    Start,
    KeyRightHeld,
    KeyLeftHeld,
    KeySpacePressed,
    KeyPressed,
    KeyHeld,
};

enum class ScriptAction {
    SetMovementSpeed,
    MoveHorizontal,
    Jump,
    CallFunction,
};

struct ScriptStatement {
    std::string nodeId;
    ScriptAction action{ScriptAction::MoveHorizontal};
    std::string targetObjectId;
    float value{0.0F};
    std::string valueVariable;
    std::string functionName;
    std::vector<std::string> functionArguments;
};

struct ScriptVariable {
    std::string nodeId;
    std::string name;
    float value{0.0F};
    bool constant{false};
};

struct ScriptFunction {
    std::string nodeId;
    std::string name;
    std::vector<std::string> parameters;
    std::vector<ScriptStatement> body;
};

struct ScriptEventHandler {
    std::string nodeId;
    ScriptEvent event{ScriptEvent::KeyRightHeld};
    std::vector<ScriptStatement> body;
    std::string key;
};

struct ScriptDocument {
    int formatVersion{3};
    std::string scriptId{"script-main"};
    std::vector<ScriptVariable> variables;
    std::vector<ScriptFunction> functions;
    std::vector<ScriptEventHandler> handlers;
};

struct ScriptDiagnostic {
    std::string message;
    std::size_t line{0};
};

enum class BytecodeOp : unsigned char {
    SetMovementSpeed = 0x01,
    MoveEntityX = 0x10,
    JumpIfGrounded = 0x20,
    Halt = 0xFF,
};

struct BytecodeInstruction {
    BytecodeOp op{BytecodeOp::Halt};
    unsigned short symbolIndex{0};
    float operand{0.0F};
    std::string sourceNodeId;
};

struct CompiledEventHandler {
    ScriptEvent event{ScriptEvent::KeyRightHeld};
    std::vector<BytecodeInstruction> bytecode;
    std::string key;
};

struct ScriptProgram {
    std::vector<std::string> entitySymbols;
    std::vector<CompiledEventHandler> handlers;
};

struct ScriptInput {
    bool rightHeld{false};
    bool leftHeld{false};
    bool spacePressed{false};
    std::unordered_set<std::string> keysPressed;
    std::unordered_set<std::string> keysHeld;
};

ScriptDocument makeDefaultScript(const Scene& scene);
std::string scriptEventName(ScriptEvent event);
std::string scriptEventName(ScriptEvent event, const std::string& key);
bool parseScriptEvent(const std::string& name, ScriptEvent& event, std::string* key = nullptr);
std::string formatScriptCode(const ScriptDocument& document, const Scene& scene);
std::string formatScriptKorean(const ScriptDocument& document, const Scene& scene);
bool parseScriptCode(const std::string& code, const Scene& scene, ScriptDocument& output,
                     ScriptDiagnostic& diagnostic);
bool parseScriptKorean(const std::string& source, const Scene& scene, ScriptDocument& output,
                       ScriptDiagnostic& diagnostic);
bool compileScript(const ScriptDocument& document, const Scene& scene, ScriptProgram& output,
                   ScriptDiagnostic& diagnostic);
void executeScriptStart(const ScriptProgram& program, Scene& scene);
void executeScript(const ScriptProgram& program, Scene& scene, const ScriptInput& input,
                   float fixedDeltaSeconds);

} // namespace jm
