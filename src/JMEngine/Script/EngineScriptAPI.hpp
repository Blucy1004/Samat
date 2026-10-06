#pragma once
#include "JMEngine/Scene/Scene.hpp"
#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/Script.hpp"

namespace jm {
// Engine-owned adapters. The language core does not depend on Scene/physics/input.
struct EngineScriptContext {
    Scene &scene;
    std::string playerId;
    ScriptInput input;
    double deltaSeconds{1.0 / 60.0};
    GameObject *player() const;
};
class EngineScriptScope {
  public:
    explicit EngineScriptScope(EngineScriptContext &context);
    ~EngineScriptScope();
    EngineScriptScope(const EngineScriptScope &) = delete;
    EngineScriptScope &operator=(const EngineScriptScope &) = delete;

  private:
    EngineScriptContext *previous_;
};
enum class EngineScriptBackend { Interpreter, Bootstrap, LLVM };
class EngineEventRuntime {
  public:
    EngineEventRuntime(Scene &scene, std::string playerId, script::Program program,
                       EngineScriptBackend backend = EngineScriptBackend::Interpreter);
    void start();
    void tick(const ScriptInput &input, double deltaSeconds);
    void dispatch(const std::string &event);

  private:
    EngineScriptContext context_;
    std::unique_ptr<script::ExecutionSession> session_;
    script::ir::Module module_;
    script::ir::NativeCode code_;
    bool started_{};
};
std::function<bool(std::string_view)> engineModuleResolver();
script::HostFunction engineHostFunctions(EngineScriptContext &context);
script::ir::NativeFunctionRegistry engineNativeFunctions();
void executeNativeEvent(const script::ir::Module &module, const script::ir::NativeCode &code,
                        const std::string &event);
} // namespace jm
