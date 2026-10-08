#pragma once

#include "JMEngine/Script/LanguageCore.hpp"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace jm::studio {

struct TextRange {
    std::size_t begin{};
    std::size_t end{};
};

enum class SyntaxKind { Keyword, Identifier, Builtin, Number, String, Comment, Operator };
struct SyntaxToken {
    TextRange range;
    SyntaxKind kind{SyntaxKind::Identifier};
};
std::optional<SyntaxToken> syntaxTokenAt(std::string_view source, std::size_t offset);

// Returns the string contents range only for input.isHeld("...") and
// input.wasPressed("...") arguments at the supplied byte cursor position.
std::optional<TextRange> inputKeyStringAt(std::string_view source, std::size_t cursor);
bool replaceInputKeyString(std::string &source, std::size_t cursor, std::string_view keyName);

bool loadScriptFile(const std::filesystem::path &path, std::string &source, std::string &error);
bool saveScriptFile(const std::filesystem::path &path, std::string_view source, std::string &error);
std::optional<std::filesystem::path> chooseOpenScriptFile(const std::filesystem::path &initialPath = {});
std::optional<std::filesystem::path> chooseSaveScriptFile(const std::filesystem::path &initialPath = {});

struct RunSnapshot {
    std::uint64_t generation{};
    bool running{};
    bool completed{};
    bool cancelled{};
    std::size_t instructionsExecuted{};
    std::vector<std::string> output;
    script::Value returnValue;
    std::string error;
};

// Owns one interpreter worker. The worker owns copies of the AST/options and
// only publishes immutable result snapshots; it never touches the UI or Scene.
class InterpreterRun {
  public:
    InterpreterRun() = default;
    ~InterpreterRun();
    InterpreterRun(const InterpreterRun &) = delete;
    InterpreterRun &operator=(const InterpreterRun &) = delete;

    bool start(script::Program program, script::RunOptions options = {});
    void requestStop();
    void joinCompleted();
    void stop();
    RunSnapshot snapshot(bool includeOutput = true) const;
    std::vector<std::string> drainOutput();

  private:
    struct State {
        mutable std::mutex mutex;
        RunSnapshot snapshot;
        std::vector<std::string> pendingOutput;
    };
    mutable std::mutex lifecycleMutex_;
    std::shared_ptr<State> state_;
    std::jthread worker_;
    std::uint64_t nextGeneration_{1};
};

} // namespace jm::studio
