#include "JMEngine/Script/Script.hpp"

#include "JMEngine/Script/KoreanParticles.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace jm {
namespace {

const GameObject* findObject(const Scene& scene, const std::string& id) {
    for (const GameObject& object : scene.objects()) if (object.id == id) return &object;
    return nullptr;
}

std::string codeIdentifier(const GameObject& object) {
    std::string identifier;
    identifier.reserve(object.name.size());
    bool previousUnderscore = false;
    for (unsigned char character : object.name) {
        if (std::isalnum(character) || character == '_') {
            identifier.push_back(static_cast<char>(character));
            previousUnderscore = character == '_';
        } else if (!previousUnderscore) {
            identifier.push_back('_');
            previousUnderscore = true;
        }
    }
    if (!identifier.empty() &&
        (std::isalpha(static_cast<unsigned char>(identifier.front())) || identifier.front() == '_')) return identifier;
    std::string suffix = object.id;
    for (char& character : suffix) if (!std::isalnum(static_cast<unsigned char>(character))) character = '_';
    return "entity_" + suffix;
}

std::string trim(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char value) { return std::isspace(value); });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char value) { return std::isspace(value); }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

class ScalarExpressionParser {
public:
    ScalarExpressionParser(const std::string& source, const std::unordered_map<std::string, float>& values,
                           bool allowUnknownNames = false)
        : source_(source), values_(values), allowUnknownNames_(allowUnknownNames) {}

    bool parse(float& value) {
        position_ = 0;
        valid_ = true;
        value = expression();
        skipSpace();
        return valid_ && position_ == source_.size() && std::isfinite(value);
    }

private:
    void skipSpace() { while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_; }
    bool take(char expected) { skipSpace(); if (position_ >= source_.size() || source_[position_] != expected) return false; ++position_; return true; }
    float expression() {
        float value = term();
        for (;;) {
            if (take('+')) value += term();
            else if (take('-')) value -= term();
            else break;
        }
        return value;
    }
    float term() {
        float value = factor();
        for (;;) {
            if (take('*')) value *= factor();
            else if (take('/')) { const float divisor = factor(); if (divisor == 0.0F) valid_ = false; else value /= divisor; }
            else break;
        }
        return value;
    }
    float factor() {
        skipSpace();
        if (take('+')) return factor();
        if (take('-')) return -factor();
        if (take('(')) { const float value = expression(); if (!take(')')) valid_ = false; return value; }
        if (position_ >= source_.size()) { valid_ = false; return 0.0F; }
        if (std::isalpha(static_cast<unsigned char>(source_[position_])) || source_[position_] == '_') {
            const std::size_t start = position_++;
            while (position_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[position_])) || source_[position_] == '_')) ++position_;
            const auto found = values_.find(source_.substr(start, position_ - start));
            if (found == values_.end()) { if (!allowUnknownNames_) valid_ = false; return 0.0F; }
            return found->second;
        }
        const char* begin = source_.c_str() + position_;
        char* end = nullptr;
        const float value = std::strtof(begin, &end);
        if (end == begin) { valid_ = false; return 0.0F; }
        position_ += static_cast<std::size_t>(end - begin);
        return value;
    }
    const std::string& source_;
    const std::unordered_map<std::string, float>& values_;
    std::size_t position_{0};
    bool valid_{true};
    bool allowUnknownNames_{false};
};

std::vector<std::string> splitArguments(const std::string& source) {
    std::vector<std::string> result;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (source[index] == '(') ++depth;
        else if (source[index] == ')') --depth;
        else if (source[index] == ',' && depth == 0) {
            result.push_back(trim(source.substr(start, index - start)));
            start = index + 1;
        }
    }
    if (start < source.size() || !trim(source).empty()) result.push_back(trim(source.substr(start)));
    if (std::any_of(result.begin(), result.end(), [](const std::string& item) { return item.empty(); })) return {};
    return result;
}

bool validScalarSyntax(const std::string& expression) {
    const std::unordered_map<std::string, float> noValues;
    float ignored = 0.0F;
    return ScalarExpressionParser(expression, noValues, true).parse(ignored);
}

std::string numberText(float value) {
    std::ostringstream output;
    output << std::setprecision(5) << std::defaultfloat << value;
    return output.str();
}

std::string callArgumentsText(const ScriptStatement& statement) {
    std::ostringstream output;
    for (std::size_t index = 0; index < statement.functionArguments.size(); ++index) {
        if (index != 0) output << ", ";
        output << statement.functionArguments[index];
    }
    return output.str();
}

std::string targetName(const Scene& scene, const std::string& id, bool korean) {
    const GameObject* object = findObject(scene, id);
    if (object == nullptr) return korean ? "없는 오브젝트" : "missing_entity";
    return korean && !object->koreanName.empty() ? object->koreanName : codeIdentifier(*object);
}

std::string eventLabel(ScriptEvent event, const std::string& key, bool korean) {
    if (!korean) return "on " + scriptEventName(event, key) + ":";
    switch (event) {
    case ScriptEvent::Start: return "시작할 때";
    case ScriptEvent::KeyRightHeld: return "오른쪽 키를 누르는 동안";
    case ScriptEvent::KeyLeftHeld: return "왼쪽 키를 누르는 동안";
    case ScriptEvent::KeySpacePressed: return "스페이스 키를 눌렀을 때";
    case ScriptEvent::KeyPressed: return "키 " + key + "를 눌렀을 때";
    case ScriptEvent::KeyHeld: return "키 " + key + "를 누르는 동안";
    }
    return {};
}

std::string retainedNodeId(const ScriptDocument& old, ScriptEvent event, ScriptAction action,
                           const std::string& target, const std::string& proposed,
                           const std::string& key = {}) {
    for (const ScriptEventHandler& handler : old.handlers) {
        if (handler.event != event) continue;
        if ((event == ScriptEvent::KeyPressed || event == ScriptEvent::KeyHeld) && handler.key != key) continue;
        for (const ScriptStatement& statement : handler.body) {
            if (statement.action == action && statement.targetObjectId == target) return statement.nodeId;
        }
    }
    return proposed;
}

const GameObject* resolveIdentifier(const Scene& scene, const std::string& name, ScriptDiagnostic& diagnostic,
                                   std::size_t line) {
    const GameObject* matched = nullptr;
    for (const GameObject& object : scene.objects()) {
        if (codeIdentifier(object) != name) continue;
        if (matched != nullptr) {
            diagnostic = {"같은 코드 이름을 가진 오브젝트가 여러 개예요. 오브젝트 이름을 고유하게 바꿔 주세요.", line};
            return nullptr;
        }
        matched = &object;
    }
    if (matched == nullptr) diagnostic = {"장면에서 오브젝트를 찾을 수 없어요: " + name, line};
    return matched;
}

const GameObject* resolveKoreanName(const Scene& scene, const std::string& name, ScriptDiagnostic& diagnostic,
                                   std::size_t line) {
    const GameObject* matched = nullptr;
    for (const GameObject& object : scene.objects()) {
        if (object.koreanName != name && object.name != name) continue;
        if (matched != nullptr) {
            diagnostic = {"같은 한글 이름을 가진 오브젝트가 여러 개예요. 이름을 고유하게 바꿔 주세요.", line};
            return nullptr;
        }
        matched = &object;
    }
    if (matched == nullptr) diagnostic = {"장면에서 오브젝트를 찾을 수 없어요: " + name, line};
    return matched;
}

std::string generatedNodeId() {
    static std::atomic_uint64_t next{1};
    return "node-generated-" + std::to_string(next.fetch_add(1, std::memory_order_relaxed));
}

unsigned short symbolIndexFor(ScriptProgram& program, const std::string& id) {
    const auto found = std::find(program.entitySymbols.begin(), program.entitySymbols.end(), id);
    if (found != program.entitySymbols.end()) return static_cast<unsigned short>(std::distance(program.entitySymbols.begin(), found));
    program.entitySymbols.push_back(id);
    return static_cast<unsigned short>(program.entitySymbols.size() - 1);
}

bool eventTriggered(ScriptEvent event, const ScriptInput& input) {
    switch (event) {
    case ScriptEvent::Start: return false;
    case ScriptEvent::KeyRightHeld: return input.rightHeld;
    case ScriptEvent::KeyLeftHeld: return input.leftHeld;
    case ScriptEvent::KeySpacePressed: return input.spacePressed;
    case ScriptEvent::KeyPressed: return false;
    case ScriptEvent::KeyHeld: return false;
    }
    return false;
}

void executeHandlers(const ScriptProgram& program, Scene& scene, ScriptEvent event, float deltaSeconds, const std::string& key = {}) {
    constexpr std::size_t instructionBudget = 1024;
    std::size_t executed = 0;
    for (const CompiledEventHandler& handler : program.handlers) {
        if (handler.event != event || ((event == ScriptEvent::KeyPressed || event == ScriptEvent::KeyHeld) && handler.key != key)) continue;
        for (const BytecodeInstruction& instruction : handler.bytecode) {
            if (++executed > instructionBudget || instruction.op == BytecodeOp::Halt) break;
            if (instruction.symbolIndex >= program.entitySymbols.size() || !std::isfinite(instruction.operand)) continue;
            const std::string& id = program.entitySymbols[instruction.symbolIndex];
            for (GameObject& object : scene.objects()) {
                if (object.id != id) continue;
                switch (instruction.op) {
                case BytecodeOp::SetMovementSpeed:
                    object.movementSpeed = instruction.operand;
                    break;
                case BytecodeOp::MoveEntityX:
                    if (deltaSeconds > 0.0F) object.position.x += instruction.operand * object.movementSpeed * deltaSeconds;
                    break;
                case BytecodeOp::JumpIfGrounded:
                    if (object.physicsEnabled && !object.isStatic && object.grounded) {
                        object.verticalVelocity = instruction.operand / object.mass;
                        object.grounded = false;
                    }
                    break;
                case BytecodeOp::Halt:
                    break;
                }
                break;
            }
        }
    }
}

} // namespace

std::string scriptEventName(ScriptEvent event) { return scriptEventName(event, {}); }

std::string scriptEventName(ScriptEvent event, const std::string& key) {
    switch (event) {
    case ScriptEvent::Start: return "start";
    case ScriptEvent::KeyRightHeld: return "key.right.held";
    case ScriptEvent::KeyLeftHeld: return "key.left.held";
    case ScriptEvent::KeySpacePressed: return "key.space.pressed";
    case ScriptEvent::KeyPressed: return "key." + key + ".pressed";
    case ScriptEvent::KeyHeld: return "key." + key + ".held";
    }
    return {};
}

bool parseScriptEvent(const std::string& name, ScriptEvent& event, std::string* key) {
    if (key) key->clear();
    if (name == "start") event = ScriptEvent::Start;
    else if (name == "key.right.held") event = ScriptEvent::KeyRightHeld;
    else if (name == "key.left.held") event = ScriptEvent::KeyLeftHeld;
    else if (name == "key.space.pressed") event = ScriptEvent::KeySpacePressed;
    else if (name.rfind("key.", 0) == 0) {
        const std::size_t ending = name.rfind(".pressed");
        const bool pressed = ending != std::string::npos && ending + 8 == name.size();
        const std::size_t heldEnding = name.rfind(".held");
        const bool held = heldEnding != std::string::npos && heldEnding + 5 == name.size();
        const std::size_t suffix = pressed ? ending : held ? heldEnding : std::string::npos;
        if (suffix == std::string::npos || suffix <= 4) return false;
        const std::string parsedKey = name.substr(4, suffix - 4);
        if (!std::all_of(parsedKey.begin(), parsedKey.end(), [](unsigned char ch) { return std::islower(ch) || std::isdigit(ch) || ch == '_'; })) return false;
        event = pressed ? ScriptEvent::KeyPressed : ScriptEvent::KeyHeld;
        if (key) *key = parsedKey;
    } else return false;
    return true;
}

ScriptDocument makeDefaultScript(const Scene& scene) {
    ScriptDocument document;
    const GameObject* player = nullptr;
    for (const GameObject& object : scene.objects()) {
        if (object.kind == ObjectKind::Sprite2D && object.physicsEnabled && !object.isStatic) {
            player = &object;
            break;
        }
    }
    if (player == nullptr) {
        for (const GameObject& object : scene.objects()) {
            if (object.kind == ObjectKind::Sprite2D && !object.isStatic) { player = &object; break; }
        }
    }
    if (player == nullptr && !scene.objects().empty()) player = &scene.objects().front();
    if (player == nullptr) return document;

    document.variables.push_back({"variable-speed", "moveSpeed", 8.0F, false});
    document.functions = {
        {"function-setup", "setupPlayer", {"speed"}, {{"node-speed", ScriptAction::SetMovementSpeed, player->id, 8.0F, "speed", {}}}},
        {"function-right", "moveRight", {}, {{"node-right", ScriptAction::MoveHorizontal, player->id, 1.0F, {}, {}}}},
        {"function-left", "moveLeft", {}, {{"node-left", ScriptAction::MoveHorizontal, player->id, -1.0F, {}, {}}}},
        {"function-jump", "jumpPlayer", {}, {{"node-jump", ScriptAction::Jump, player->id, 12.0F, {}, {}}}},
    };
    document.handlers = {
        {"handler-start", ScriptEvent::Start, {{"call-setup", ScriptAction::CallFunction, {}, 0.0F, {}, "setupPlayer", {"moveSpeed"}}}},
        {"handler-right", ScriptEvent::KeyRightHeld, {{"call-right", ScriptAction::CallFunction, {}, 0.0F, {}, "moveRight"}}},
        {"handler-left", ScriptEvent::KeyLeftHeld, {{"call-left", ScriptAction::CallFunction, {}, 0.0F, {}, "moveLeft"}}},
        {"handler-jump", ScriptEvent::KeySpacePressed, {{"call-jump", ScriptAction::CallFunction, {}, 0.0F, {}, "jumpPlayer"}}},
    };
    return document;
}

std::string formatScriptCode(const ScriptDocument& document, const Scene& scene) {
    std::ostringstream output;
    for (const ScriptVariable& variable : document.variables) {
        output << (variable.constant ? "const " : "let ") << variable.name << " = " << numberText(variable.value) << '\n';
    }
    if (!document.variables.empty()) output << '\n';
    for (const ScriptFunction& function : document.functions) {
        output << "fn " << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) { if (i != 0) output << ", "; output << function.parameters[i]; }
        output << "):\n";
        for (const ScriptStatement& statement : function.body) {
            if (statement.action == ScriptAction::CallFunction) {
                output << "    " << statement.functionName << '(' << callArgumentsText(statement) << ")\n";
                continue;
            }
            const std::string name = targetName(scene, statement.targetObjectId, false);
            switch (statement.action) {
            case ScriptAction::SetMovementSpeed:
                output << "    " << name << ".speed = " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << '\n';
                break;
            case ScriptAction::MoveHorizontal:
                output << "    " << name << ".move(direction: " << (statement.value < 0.0F ? "left" : "right") << ")\n";
                break;
            case ScriptAction::Jump:
                output << "    if " << name << ".grounded:\n"
                       << "        " << name << ".jump(force: " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << ")\n";
                break;
            case ScriptAction::CallFunction: break;
            }
        }
        output << '\n';
    }
    for (const ScriptEventHandler& handler : document.handlers) {
        output << eventLabel(handler.event, handler.key, false) << '\n';
        for (const ScriptStatement& statement : handler.body) {
            if (statement.action == ScriptAction::CallFunction) {
                output << "    " << statement.functionName << '(' << callArgumentsText(statement) << ")\n";
                continue;
            }
            const std::string name = targetName(scene, statement.targetObjectId, false);
            switch (statement.action) {
            case ScriptAction::SetMovementSpeed:
                output << "    " << name << ".speed = " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << '\n';
                break;
            case ScriptAction::MoveHorizontal:
                output << "    " << name << ".move(direction: " << (statement.value < 0.0F ? "left" : "right") << ")\n";
                break;
            case ScriptAction::Jump:
                output << "    if " << name << ".grounded:\n"
                       << "        " << name << ".jump(force: " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << ")\n";
                break;
            case ScriptAction::CallFunction: break;
            }
        }
    }
    return output.str();
}

std::string formatScriptKorean(const ScriptDocument& document, const Scene& scene) {
    std::ostringstream output;
    for (const ScriptVariable& variable : document.variables) {
        output << (variable.constant ? "숫자 상수 " : "숫자 변수 ") << variable.name << " 를 " << numberText(variable.value) << "로 정한다.\n";
    }
    if (!document.variables.empty()) output << '\n';
    for (const ScriptFunction& function : document.functions) {
        output << "함수 " << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) { if (i != 0) output << ", "; output << function.parameters[i]; }
        output << "):\n";
        for (const ScriptStatement& statement : function.body) {
            if (statement.action == ScriptAction::CallFunction) output << "    " << statement.functionName << '(' << callArgumentsText(statement) << ")\n";
            else if (statement.action == ScriptAction::SetMovementSpeed)
                output << "    " << targetName(scene, statement.targetObjectId, true) << "의 이동 속도를 "
                       << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << "로 정한다.\n";
            else if (statement.action == ScriptAction::MoveHorizontal)
                output << "    " << attachKoreanParticle(targetName(scene, statement.targetObjectId, true), KoreanParticle::Object)
                       << (statement.value < 0.0F ? " 왼쪽으로 움직인다.\n" : " 오른쪽으로 움직인다.\n");
            else if (statement.action == ScriptAction::Jump)
                output << "    " << attachKoreanParticle(targetName(scene, statement.targetObjectId, true), KoreanParticle::Subject)
                       << " 땅에 닿아 있다면\n        " << attachKoreanParticle(targetName(scene, statement.targetObjectId, true), KoreanParticle::Object)
                       << " 힘 " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << "로 점프시킨다.\n";
        }
        output << '\n';
    }
    for (const ScriptEventHandler& handler : document.handlers) {
        output << eventLabel(handler.event, handler.key, true) << '\n';
        for (const ScriptStatement& statement : handler.body) {
            if (statement.action == ScriptAction::CallFunction) {
                output << "    " << statement.functionName << '(' << callArgumentsText(statement) << ")\n";
                continue;
            }
            const std::string name = targetName(scene, statement.targetObjectId, true);
            switch (statement.action) {
            case ScriptAction::SetMovementSpeed:
                output << "    " << name << "의 이동 속도를 " << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << "로 정한다.\n";
                break;
            case ScriptAction::MoveHorizontal:
                output << "    " << attachKoreanParticle(name, KoreanParticle::Object) << ' '
                       << (statement.value < 0.0F ? "왼쪽으로" : "오른쪽으로") << " 움직인다.\n";
                break;
            case ScriptAction::Jump:
                output << "    " << attachKoreanParticle(name, KoreanParticle::Subject) << " 땅에 닿아 있다면\n"
                       << "        " << attachKoreanParticle(name, KoreanParticle::Object) << " 힘 "
                       << (statement.valueVariable.empty() ? numberText(statement.value) : statement.valueVariable) << "로 점프시킨다.\n";
                break;
            case ScriptAction::CallFunction: break;
            }
        }
        output << '\n';
    }
    return output.str();
}

bool parseScriptCode(const std::string& code, const Scene& scene, ScriptDocument& output,
                     ScriptDiagnostic& diagnostic) {
    static const std::regex variablePattern(R"(^\s*(let|const)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))\s*$)");
    static const std::regex functionPattern(R"(^fn\s+([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\)\s*:$)");
    static const std::regex callPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\((.*)\)$)");
    static const std::regex variableNamePattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    static const std::regex speedPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\.speed\s*=\s*(.+)$)");
    static const std::regex movePattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\.move\(\s*direction\s*:\s*(right|left)\s*\)$)");
    static const std::regex moveLegacyPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\.move\(\s*x\s*:\s*(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))\s*\)$)");
    static const std::regex groundedPattern(R"(^if\s+([A-Za-z_][A-Za-z0-9_]*)\.grounded\s*:$)");
    static const std::regex jumpPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\.jump\(\s*force\s*:\s*(.+)\s*\)$)");

    ScriptDocument parsed;
    parsed.scriptId = output.scriptId;
    ScriptEventHandler* current = nullptr;
    ScriptFunction* currentFunction = nullptr;
    bool expectsGroundedJump = false;
    std::string groundedTarget;
    const auto readNumber = [](const std::string& text, float& value) {
        try {
            std::size_t consumed = 0;
            value = std::stof(text, &consumed);
            return consumed == text.size() && std::isfinite(value);
        } catch (const std::exception&) {
            return false;
        }
    };
    std::istringstream lines(code);
    std::string rawLine;
    std::size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        const std::string line = trim(rawLine);
        if (line.empty() || line[0] == '#') continue;
        std::smatch match;
        if (std::regex_match(line, match, variablePattern)) {
            if (!parsed.functions.empty() || !parsed.handlers.empty()) {
                diagnostic = {"변수는 함수와 이벤트보다 먼저 선언해 주세요.", lineNumber};
                return false;
            }
            float value = 0.0F;
            if (!readNumber(match[3].str(), value) || value < -1000000.0F || value > 1000000.0F) {
                diagnostic = {"변수 값이 허용 범위를 벗어났어요.", lineNumber};
                return false;
            }
            const std::string name = match[2].str();
            if (std::any_of(parsed.variables.begin(), parsed.variables.end(), [&](const ScriptVariable& variable) { return variable.name == name; })) {
                diagnostic = {"변수 이름이 중복돼요: " + name, lineNumber};
                return false;
            }
            parsed.variables.push_back({generatedNodeId(), name, value, match[1].str() == "const"});
            continue;
        }
        if (std::regex_match(line, match, functionPattern)) {
            const std::string name = match[1].str();
            if (std::any_of(parsed.functions.begin(), parsed.functions.end(), [&](const ScriptFunction& function) { return function.name == name; })) {
                diagnostic = {"함수 이름이 중복돼요: " + name, lineNumber};
                return false;
            }
            ScriptFunction function;
            function.nodeId = generatedNodeId();
            function.name = name;
            const std::string parameterText = trim(match[2].str());
            if (!parameterText.empty()) {
                function.parameters = splitArguments(parameterText);
                for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                    if (!std::regex_match(function.parameters[index], variableNamePattern) ||
                        std::find(function.parameters.begin(), function.parameters.begin() + static_cast<std::ptrdiff_t>(index), function.parameters[index]) != function.parameters.begin() + static_cast<std::ptrdiff_t>(index)) {
                        diagnostic = {"함수 매개변수는 쉼표로 나눈 고유한 이름이어야 해요.", lineNumber};
                        return false;
                    }
                }
            }
            parsed.functions.push_back(std::move(function));
            currentFunction = &parsed.functions.back();
            current = nullptr;
            expectsGroundedJump = false;
            groundedTarget.clear();
            continue;
        }
        if (line.rfind("on ", 0) == 0 && line.back() == ':') {
            const std::string eventText = line.substr(3, line.size() - 4);
            ScriptEvent event{};
            std::string key;
            if (!parseScriptEvent(eventText, event, &key)) {
                diagnostic = {"아직 지원하지 않는 이벤트예요: " + eventText, lineNumber};
                return false;
            }
            parsed.handlers.push_back({generatedNodeId(), event, {}, key});
            current = &parsed.handlers.back();
            currentFunction = nullptr;
            expectsGroundedJump = false;
            groundedTarget.clear();
            continue;
        }
        if (current == nullptr && currentFunction == nullptr) {
            diagnostic = {"문장은 함수 또는 'on ...:' 이벤트 안에 작성해 주세요.", lineNumber};
            return false;
        }

        if (std::regex_match(line, match, groundedPattern)) {
            if (currentFunction == nullptr && current->event != ScriptEvent::KeySpacePressed) {
                diagnostic = {"땅에 닿았는지 확인하는 조건은 점프 이벤트 안에서 사용해 주세요.", lineNumber};
                return false;
            }
            groundedTarget = match[1].str();
            expectsGroundedJump = true;
            continue;
        }

        ScriptAction action{};
        std::string name;
        std::string valueVariable;
        float value = 0.0F;
        if (std::regex_match(line, match, speedPattern)) {
            name = match[1].str();
            action = ScriptAction::SetMovementSpeed;
            if (!readNumber(match[2].str(), value)) valueVariable = trim(match[2].str());
            if (currentFunction == nullptr && current->event != ScriptEvent::Start) {
                diagnostic = {"속도 설정은 시작 이벤트 안에 작성해 주세요.", lineNumber};
                return false;
            }
        } else if (std::regex_match(line, match, movePattern)) {
            name = match[1].str();
            action = ScriptAction::MoveHorizontal;
            value = match[2].str() == "left" ? -1.0F : 1.0F;
            if (currentFunction == nullptr && current->event != ScriptEvent::KeyRightHeld && current->event != ScriptEvent::KeyLeftHeld) {
                diagnostic = {"좌우 이동은 오른쪽 또는 왼쪽 키 이벤트 안에 작성해 주세요.", lineNumber};
                return false;
            }
        } else if (std::regex_match(line, match, moveLegacyPattern)) {
            name = match[1].str();
            action = ScriptAction::MoveHorizontal;
            if (!readNumber(match[2].str(), value)) { diagnostic = {"숫자 값이 너무 크거나 올바르지 않아요.", lineNumber}; return false; }
            if (currentFunction == nullptr && current->event != ScriptEvent::KeyRightHeld && current->event != ScriptEvent::KeyLeftHeld) {
                diagnostic = {"좌우 이동은 오른쪽 또는 왼쪽 키 이벤트 안에 작성해 주세요.", lineNumber};
                return false;
            }
        } else if (std::regex_match(line, match, jumpPattern)) {
            name = match[1].str();
            action = ScriptAction::Jump;
            if (!readNumber(match[2].str(), value)) valueVariable = trim(match[2].str());
            if (currentFunction == nullptr && current->event != ScriptEvent::KeySpacePressed) {
                diagnostic = {"점프 동작은 스페이스 키 이벤트 안에 작성해 주세요.", lineNumber};
                return false;
            }
            if (currentFunction == nullptr && expectsGroundedJump && groundedTarget != name) {
                diagnostic = {"조건을 확인한 오브젝트와 점프하는 오브젝트가 달라요.", lineNumber};
                return false;
            }
            expectsGroundedJump = false;
        } else {
            if (std::regex_match(line, match, callPattern)) {
                ScriptStatement call{generatedNodeId(), ScriptAction::CallFunction, {}, 0.0F, {}, match[1].str()};
                const std::string argumentsText = trim(match[2].str());
                if (!argumentsText.empty()) {
                    call.functionArguments = splitArguments(argumentsText);
                    if (call.functionArguments.empty()) { diagnostic = {"함수 인자 사이의 쉼표 위치를 확인해 주세요.", lineNumber}; return false; }
                    for (const std::string& argument : call.functionArguments) {
                        if (!validScalarSyntax(argument)) { diagnostic = {"함수 인자에 올바른 숫자 식을 써 주세요.", lineNumber}; return false; }
                    }
                }
                if (currentFunction != nullptr) currentFunction->body.push_back(std::move(call));
                else current->body.push_back(std::move(call));
                continue;
            }
            diagnostic = {"이 문장은 아직 지원하지 않아요. let, fn, 이동·점프 문장 또는 함수 호출을 사용해 주세요.", lineNumber};
            return false;
        }

        if (!std::isfinite(value)) {
            diagnostic = {"숫자 값이 올바르지 않아요.", lineNumber};
            return false;
        }
        if (!valueVariable.empty() && !validScalarSyntax(valueVariable)) {
            diagnostic = {"속도나 점프 힘에 올바른 숫자 식을 써 주세요.", lineNumber};
            return false;
        }
        if (valueVariable.empty() && ((action == ScriptAction::SetMovementSpeed && (value < 0.0F || value > 100.0F)) ||
            (action == ScriptAction::MoveHorizontal && std::abs(value) > 1.0F) ||
            (action == ScriptAction::Jump && (value <= 0.0F || value > 100.0F)))) {
            diagnostic = {"속도·방향·점프 힘의 값이 허용 범위를 벗어났어요.", lineNumber};
            return false;
        }
        const GameObject* object = resolveIdentifier(scene, name, diagnostic, lineNumber);
        if (object == nullptr) return false;
        const std::string newId = generatedNodeId();
        const ScriptEvent eventForIdentity = current == nullptr ? ScriptEvent::Start : current->event;
        const std::string nodeId = retainedNodeId(output, eventForIdentity, action, object->id, newId,
                                                  current == nullptr ? std::string{} : current->key);
        ScriptStatement statement{nodeId, action, object->id, value, valueVariable, {}};
        if (currentFunction != nullptr) currentFunction->body.push_back(std::move(statement));
        else current->body.push_back(std::move(statement));
    }

    if (expectsGroundedJump) {
        diagnostic = {"땅에 닿았는지 확인한 뒤 실행할 점프 동작이 빠졌어요.", lineNumber};
        return false;
    }
    if (parsed.handlers.empty()) {
        diagnostic = {"실행할 이벤트를 하나 이상 작성해 주세요.", 0};
        return false;
    }
    for (ScriptEventHandler& handler : parsed.handlers) {
        for (const ScriptEventHandler& old : output.handlers) {
            if (old.event == handler.event) { handler.nodeId = old.nodeId; break; }
        }
    }
    output = std::move(parsed);
    diagnostic = {};
    return true;
}

bool parseScriptKorean(const std::string& source, const Scene& scene, ScriptDocument& output,
                       ScriptDiagnostic& diagnostic) {
    static const std::regex variablePattern(R"(^숫자 (변수|상수) ([A-Za-z_][A-Za-z0-9_]*) 를 (-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))로 정한다\.$)");
    static const std::regex functionPattern(R"(^함수 ([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\):$)");
    static const std::regex callPattern(R"(^([A-Za-z_][A-Za-z0-9_]*)\((.*)\)$)");
    static const std::regex speedPattern(R"(^(.+)의 이동 속도를 (.+)로 정한다\.$)");
    static const std::regex movePattern(R"(^(.+?)(?:을|를) (왼쪽|오른쪽)으로 움직인다\.$)");
    static const std::regex groundedPattern(R"(^(.+?)(?:이|가) 땅에 닿아 있다면$)");
    static const std::regex jumpPattern(R"(^(.+?)(?:을|를) 힘 (.+)로 점프시킨다\.$)");
    ScriptDocument parsed;
    parsed.scriptId = output.scriptId;
    ScriptEventHandler* current = nullptr;
    ScriptFunction* currentFunction = nullptr;
    bool expectsJump = false;
    std::string groundedName;
    std::istringstream lines(source);
    std::string rawLine;
    std::size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        const std::string line = trim(rawLine);
        if (line.empty() || line[0] == '#') continue;
        std::smatch match;
        if (std::regex_match(line, match, variablePattern)) {
            if (!parsed.functions.empty() || !parsed.handlers.empty()) { diagnostic = {"변수는 함수와 이벤트보다 먼저 선언해 주세요.", lineNumber}; return false; }
            float variableValue = 0.0F;
            try { variableValue = std::stof(match[3].str()); } catch (const std::exception&) { diagnostic = {"변수 숫자를 확인해 주세요.", lineNumber}; return false; }
            if (!std::isfinite(variableValue) || std::abs(variableValue) > 1000000.0F) { diagnostic = {"변수 값이 허용 범위를 벗어났어요.", lineNumber}; return false; }
            const std::string variableName = match[2].str();
            if (std::any_of(parsed.variables.begin(), parsed.variables.end(), [&](const ScriptVariable& item) { return item.name == variableName; })) {
                diagnostic = {"변수 이름이 중복돼요: " + variableName, lineNumber}; return false;
            }
            parsed.variables.push_back({generatedNodeId(), variableName, variableValue, match[1].str() == "상수"});
            continue;
        }
        if (std::regex_match(line, match, functionPattern)) {
            const std::string functionName = match[1].str();
            if (std::any_of(parsed.functions.begin(), parsed.functions.end(), [&](const ScriptFunction& item) { return item.name == functionName; })) {
                diagnostic = {"함수 이름이 중복돼요: " + functionName, lineNumber}; return false;
            }
            ScriptFunction function;
            function.nodeId = generatedNodeId();
            function.name = functionName;
            const std::string parameterText = trim(match[2].str());
            if (!parameterText.empty()) {
                function.parameters = splitArguments(parameterText);
                for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                    if (!std::regex_match(function.parameters[index], std::regex(R"(^[A-Za-z_][A-Za-z0-9_]*$)")) ||
                        std::find(function.parameters.begin(), function.parameters.begin() + static_cast<std::ptrdiff_t>(index), function.parameters[index]) != function.parameters.begin() + static_cast<std::ptrdiff_t>(index)) {
                        diagnostic = {"함수 매개변수는 쉼표로 나눈 고유한 이름이어야 해요.", lineNumber};
                        return false;
                    }
                }
            }
            parsed.functions.push_back(std::move(function));
            currentFunction = &parsed.functions.back();
            current = nullptr;
            expectsJump = false;
            continue;
        }
        ScriptEvent event{};
        std::string key;
        bool isEvent = true;
        if (line == eventLabel(ScriptEvent::Start, {}, true)) event = ScriptEvent::Start;
        else if (line == eventLabel(ScriptEvent::KeyRightHeld, {}, true)) event = ScriptEvent::KeyRightHeld;
        else if (line == eventLabel(ScriptEvent::KeyLeftHeld, {}, true)) event = ScriptEvent::KeyLeftHeld;
        else if (line == eventLabel(ScriptEvent::KeySpacePressed, {}, true)) event = ScriptEvent::KeySpacePressed;
        else if (line.rfind("키 ", 0) == 0) {
            const auto pressed = line.find("를 눌렀을 때");
            const auto held = line.find("를 누르는 동안");
            if (pressed != std::string::npos && pressed + std::string("를 눌렀을 때").size() == line.size()) {
                event = ScriptEvent::KeyPressed; key = line.substr(3, pressed - 3);
            } else if (held != std::string::npos && held + std::string("를 누르는 동안").size() == line.size()) {
                event = ScriptEvent::KeyHeld; key = line.substr(3, held - 3);
            } else isEvent = false;
        } else isEvent = false;
        if (isEvent) {
            parsed.handlers.push_back({generatedNodeId(), event, {}, key});
            current = &parsed.handlers.back();
            currentFunction = nullptr;
            expectsJump = false;
            groundedName.clear();
            continue;
        }
        if (current == nullptr && currentFunction == nullptr) {
            diagnostic = {"먼저 함수 또는 이벤트를 작성해 주세요.", lineNumber};
            return false;
        }
        ScriptAction action{};
        std::string name;
        std::string valueVariable;
        float value = 0.0F;
        if (std::regex_match(line, match, groundedPattern)) {
            if (currentFunction == nullptr && current->event != ScriptEvent::KeySpacePressed) {
                diagnostic = {"땅에 닿았는지 확인하는 조건은 점프 이벤트 안에 작성해 주세요.", lineNumber};
                return false;
            }
            groundedName = match[1].str();
            expectsJump = true;
            continue;
        }
        try {
            if (std::regex_match(line, match, speedPattern)) {
                if (currentFunction == nullptr && current->event != ScriptEvent::Start) { diagnostic = {"이동 속도는 시작할 때 정해 주세요.", lineNumber}; return false; }
                name = match[1].str();
                try { value = std::stof(match[2].str()); } catch (const std::exception&) { valueVariable = trim(match[2].str()); }
                action = ScriptAction::SetMovementSpeed;
            } else if (std::regex_match(line, match, movePattern)) {
                if (currentFunction == nullptr && current->event != ScriptEvent::KeyRightHeld && current->event != ScriptEvent::KeyLeftHeld) { diagnostic = {"이동은 방향키 이벤트 안에 작성해 주세요.", lineNumber}; return false; }
                name = match[1].str(); value = match[2].str() == "왼쪽" ? -1.0F : 1.0F; action = ScriptAction::MoveHorizontal;
            } else if (std::regex_match(line, match, jumpPattern)) {
                if (currentFunction == nullptr && current->event != ScriptEvent::KeySpacePressed) { diagnostic = {"점프는 스페이스 키 이벤트 안에 작성해 주세요.", lineNumber}; return false; }
                name = match[1].str();
                try { value = std::stof(match[2].str()); } catch (const std::exception&) { valueVariable = trim(match[2].str()); }
                action = ScriptAction::Jump;
                if (currentFunction == nullptr && expectsJump && groundedName != name) { diagnostic = {"조건을 확인한 오브젝트와 점프하는 오브젝트가 달라요.", lineNumber}; return false; }
                expectsJump = false;
            } else {
                if (std::regex_match(line, match, callPattern)) {
                    ScriptStatement call{generatedNodeId(), ScriptAction::CallFunction, {}, 0.0F, {}, match[1].str()};
                    const std::string argumentsText = trim(match[2].str());
                    if (!argumentsText.empty()) {
                        call.functionArguments = splitArguments(argumentsText);
                        if (call.functionArguments.empty()) { diagnostic = {"함수 인자 사이의 쉼표 위치를 확인해 주세요.", lineNumber}; return false; }
                        for (const std::string& argument : call.functionArguments) {
                            if (!validScalarSyntax(argument)) { diagnostic = {"함수 인자에 올바른 숫자 식을 써 주세요.", lineNumber}; return false; }
                        }
                    }
                    if (currentFunction != nullptr) currentFunction->body.push_back(std::move(call));
                    else current->body.push_back(std::move(call));
                    continue;
                }
                diagnostic = {"지원하지 않는 문장이에요. 변수, 함수, 이동·점프 문장 형식을 사용해 주세요.", lineNumber};
                return false;
            }
        } catch (const std::exception&) {
            diagnostic = {"숫자 값이 너무 크거나 올바르지 않아요.", lineNumber};
            return false;
        }
        if (!std::isfinite(value) || (valueVariable.empty() && action == ScriptAction::SetMovementSpeed && (value < 0.0F || value > 100.0F)) ||
            (valueVariable.empty() && action == ScriptAction::Jump && (value <= 0.0F || value > 100.0F))) {
            diagnostic = {"속도 또는 점프 힘이 허용 범위를 벗어났어요.", lineNumber};
            return false;
        }
        if (!valueVariable.empty() && !validScalarSyntax(valueVariable)) {
            diagnostic = {"속도나 점프 힘에 올바른 숫자 식을 써 주세요.", lineNumber};
            return false;
        }
        const GameObject* object = resolveKoreanName(scene, name, diagnostic, lineNumber);
        if (object == nullptr) return false;
        const std::string nodeId = retainedNodeId(output, current == nullptr ? ScriptEvent::Start : current->event,
                                                   action, object->id, generatedNodeId(),
                                                   current == nullptr ? std::string{} : current->key);
        ScriptStatement statement{nodeId, action, object->id, value, valueVariable, {}};
        if (currentFunction != nullptr) currentFunction->body.push_back(std::move(statement));
        else current->body.push_back(std::move(statement));
    }
    if (expectsJump) { diagnostic = {"땅에 닿았는지 확인한 뒤 실행할 점프 동작이 빠졌어요.", lineNumber}; return false; }
    if (parsed.handlers.empty()) { diagnostic = {"실행할 이벤트를 하나 이상 작성해 주세요.", 0}; return false; }
    for (ScriptEventHandler& handler : parsed.handlers) {
        for (const ScriptEventHandler& old : output.handlers) if (old.event == handler.event) { handler.nodeId = old.nodeId; break; }
    }
    output = std::move(parsed);
    diagnostic = {};
    return true;
}

bool compileScript(const ScriptDocument& document, const Scene& scene, ScriptProgram& output,
                   ScriptDiagnostic& diagnostic) {
    ScriptProgram compiled;
    std::unordered_map<std::string, float> variables;
    for (const ScriptVariable& variable : document.variables) {
        if (variable.name.empty() || !std::isfinite(variable.value) ||
            !variables.emplace(variable.name, variable.value).second) {
            diagnostic.message = "변수 이름이 비어 있거나 중복되었어요.";
            return false;
        }
    }
    std::unordered_map<std::string, bool> functionNames;
    const std::regex validIdentifier(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    for (const ScriptFunction& function : document.functions) {
        if (!std::regex_match(function.name, validIdentifier) || !functionNames.emplace(function.name, true).second) {
            diagnostic = {"함수 이름이 비어 있거나 중복되었어요.", 0};
            return false;
        }
        std::unordered_map<std::string, bool> parameterNames;
        for (const std::string& parameter : function.parameters) {
            if (!std::regex_match(parameter, validIdentifier) || !parameterNames.emplace(parameter, true).second) {
                diagnostic = {"함수 '" + function.name + "'의 매개변수 이름을 확인해 주세요.", 0};
                return false;
            }
        }
    }
    for (const ScriptEventHandler& handler : document.handlers) {
        CompiledEventHandler code;
        code.event = handler.event;
        code.key = handler.key;
        std::vector<std::string> callStack;
        std::function<bool(const std::vector<ScriptStatement>&, const std::unordered_map<std::string, float>&)> lowerStatements;
        lowerStatements = [&](const std::vector<ScriptStatement>& statements,
                              const std::unordered_map<std::string, float>& localValues) {
        for (const ScriptStatement& statement : statements) {
            if (statement.action == ScriptAction::CallFunction) {
                const auto function = std::find_if(document.functions.begin(), document.functions.end(),
                    [&](const ScriptFunction& item) { return item.name == statement.functionName; });
                if (function == document.functions.end()) {
                    diagnostic.message = "함수를 찾을 수 없어요: " + statement.functionName;
                    return false;
                }
                if (function->parameters.size() != statement.functionArguments.size()) {
                    diagnostic.message = "함수 '" + function->name + "'에 필요한 인자 수는 " +
                        std::to_string(function->parameters.size()) + "개예요.";
                    return false;
                }
                if (callStack.size() >= 32 || std::find(callStack.begin(), callStack.end(), function->name) != callStack.end()) {
                    diagnostic.message = "함수가 자기 자신을 반복 호출하고 있어요: " + function->name;
                    return false;
                }
                std::unordered_map<std::string, float> functionValues;
                for (std::size_t index = 0; index < function->parameters.size(); ++index) {
                    std::unordered_map<std::string, float> visibleValues = variables;
                    for (const auto& [name, value] : localValues) visibleValues[name] = value;
                    float argument = 0.0F;
                    ScalarExpressionParser parser(statement.functionArguments[index], visibleValues);
                    if (!parser.parse(argument)) {
                        diagnostic.message = "함수 인자 숫자 식을 계산할 수 없어요: " + statement.functionArguments[index];
                        return false;
                    }
                    functionValues.emplace(function->parameters[index], argument);
                }
                callStack.push_back(function->name);
                const bool succeeded = lowerStatements(function->body, functionValues);
                callStack.pop_back();
                if (!succeeded) return false;
                continue;
            }
            const GameObject* target = findObject(scene, statement.targetObjectId);
            if (target == nullptr) {
                diagnostic.message = "스크립트가 참조하는 오브젝트가 장면에 없어요.";
                return false;
            }
            if ((handler.event == ScriptEvent::KeySpacePressed || !callStack.empty()) && statement.action == ScriptAction::Jump &&
                (!target->physicsEnabled || target->isStatic)) {
                diagnostic.message = "점프하려면 움직일 수 있는 물리 오브젝트가 필요해요.";
                return false;
            }
            float operand = statement.value;
            if (!statement.valueVariable.empty()) {
                std::unordered_map<std::string, float> visibleValues = variables;
                for (const auto& [name, value] : localValues) visibleValues[name] = value;
                ScalarExpressionParser parser(statement.valueVariable, visibleValues);
                if (!parser.parse(operand)) {
                    diagnostic.message = "숫자 변수나 식을 확인해 주세요: " + statement.valueVariable;
                    return false;
                }
            }
            if (!std::isfinite(operand)) {
                diagnostic.message = "숫자 값이 올바르지 않아요.";
                return false;
            }
            if ((statement.action == ScriptAction::SetMovementSpeed && (operand < 0.0F || operand > 100.0F)) ||
                (statement.action == ScriptAction::Jump && (operand <= 0.0F || operand > 100.0F)) ||
                (statement.action == ScriptAction::MoveHorizontal && std::abs(operand) > 1.0F)) {
                diagnostic.message = "계산된 속도·방향·점프 힘이 허용 범위를 벗어났어요.";
                return false;
            }
            BytecodeOp op{};
            if (statement.action == ScriptAction::SetMovementSpeed && handler.event == ScriptEvent::Start) {
                op = BytecodeOp::SetMovementSpeed;
            } else if (statement.action == ScriptAction::MoveHorizontal &&
                       (handler.event == ScriptEvent::KeyRightHeld || handler.event == ScriptEvent::KeyLeftHeld)) {
                op = BytecodeOp::MoveEntityX;
            } else if (statement.action == ScriptAction::MoveHorizontal && !callStack.empty()) {
                op = BytecodeOp::MoveEntityX;
            } else if (statement.action == ScriptAction::SetMovementSpeed && !callStack.empty()) {
                op = BytecodeOp::SetMovementSpeed;
            } else if (statement.action == ScriptAction::Jump && (handler.event == ScriptEvent::KeySpacePressed || !callStack.empty())) {
                op = BytecodeOp::JumpIfGrounded;
            } else {
                diagnostic.message = "이 동작은 현재 이벤트에서 실행할 수 없어요.";
                return false;
            }
            code.bytecode.push_back({op, symbolIndexFor(compiled, statement.targetObjectId),
                                     operand, statement.nodeId});
        }
        return true;
        };
        if (!lowerStatements(handler.body, {})) return false;
        code.bytecode.push_back({BytecodeOp::Halt, 0, 0.0F, {}});
        compiled.handlers.push_back(std::move(code));
    }
    output = std::move(compiled);
    diagnostic = {};
    return true;
}

void executeScriptStart(const ScriptProgram& program, Scene& scene) {
    executeHandlers(program, scene, ScriptEvent::Start, 0.0F);
}

void executeScript(const ScriptProgram& program, Scene& scene, const ScriptInput& input,
                   float fixedDeltaSeconds) {
    if (fixedDeltaSeconds <= 0.0F) return;
    if (input.rightHeld) executeHandlers(program, scene, ScriptEvent::KeyRightHeld, fixedDeltaSeconds);
    if (input.leftHeld) executeHandlers(program, scene, ScriptEvent::KeyLeftHeld, fixedDeltaSeconds);
    if (input.spacePressed) executeHandlers(program, scene, ScriptEvent::KeySpacePressed, fixedDeltaSeconds);
    for (const std::string& key : input.keysPressed) executeHandlers(program, scene, ScriptEvent::KeyPressed, fixedDeltaSeconds, key);
    for (const std::string& key : input.keysHeld) executeHandlers(program, scene, ScriptEvent::KeyHeld, fixedDeltaSeconds, key);
}

} // namespace jm
