#include "JMEngine/Core/SamatStudioSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace jm::studio {
namespace {
constexpr std::size_t maximumScriptBytes = 16 * 1024 * 1024;

const std::unordered_set<std::string_view> &samatKeywords() {
    static const std::unordered_set<std::string_view> values{
        "let", "const", "fn", "if", "else", "while", "for", "in", "return", "break", "continue",
        "true", "false", "null", "and", "or", "not", "import", "on", "enum", "struct",
        "함수", "변수", "상수", "라면", "아니라면", "동안", "반환한다", "반복하며", "반복을",
        "멈춘다", "다음", "진행한다", "가져온다", "실행한다", "출력한다", "참", "거짓", "시작할", "때"};
    return values;
}

const std::unordered_set<std::string_view> &samatBuiltins() {
    static const std::unordered_set<std::string_view> values{
        "print", "println", "assert", "len", "length", "abs", "min", "max", "round", "floor", "ceil",
        "sqrt", "pow", "sin", "cos", "tan", "int", "float", "string", "bool", "bitXor", "vector2",
        "vector3", "color", "range", "append", "push", "pop", "clear", "Vector2", "Vector3", "Color"};
    return values;
}

bool validKeyName(std::string_view name) {
    return !name.empty() && name.size() <= 32 &&
           std::all_of(name.begin(), name.end(), [](unsigned char character) {
               return (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '_' ||
                      character == '-' || character == '`' || character == '=' ||
                      character == '[' || character == ']' || character == ';' ||
                      character == '\'' || character == ',' || character == '.' ||
                      character == '/' || character == '\\';
           });
}

bool hasScriptExtension(const std::filesystem::path &path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
    });
    return extension == ".st";
}

std::optional<TextRange> findInputKeyString(std::string_view source, std::size_t cursor) {
    if (cursor > source.size())
        return std::nullopt;
    static const std::regex callTail(
        R"((^|[^A-Za-z0-9_.])input\.(isHeld|wasPressed)\s*\(\s*$)");
    bool comment = false;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char character = source[index];
        if (comment) {
            if (character == '\n')
                comment = false;
            continue;
        }
        if (character == '#') {
            comment = true;
            continue;
        }
        if (character != '"')
            continue;
        const std::size_t quote = index;
        std::size_t end = quote + 1;
        bool escaped = false;
        for (; end < source.size(); ++end) {
            const char current = source[end];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (current == '\\') {
                escaped = true;
                continue;
            }
            if (current == '"')
                break;
        }
        if (end == source.size())
            return std::nullopt;
        if (cursor >= quote + 1 && cursor <= end) {
            const std::string prefix(source.substr(0, quote));
            std::smatch match;
            if (std::regex_search(prefix, match, callTail))
                return TextRange{quote + 1, end};
            return std::nullopt;
        }
        index = end;
    }
    return std::nullopt;
}
} // namespace

std::optional<SyntaxToken> syntaxTokenAt(std::string_view source, std::size_t offset) {
    if (offset >= source.size())
        return std::nullopt;
    const unsigned char first = static_cast<unsigned char>(source[offset]);
    if (std::isspace(first))
        return std::nullopt;
    std::size_t end = offset + 1;
    if (source[offset] == '#') {
        end = source.find('\n', offset);
        if (end == std::string_view::npos)
            end = source.size();
        return SyntaxToken{{offset, end}, SyntaxKind::Comment};
    }
    if (source[offset] == '"' || source[offset] == '\'') {
        const char quote = source[offset];
        bool escaped = false;
        while (end < source.size()) {
            if (escaped) {
                escaped = false;
                ++end;
                continue;
            }
            if (source[end] == '\\') {
                escaped = true;
                ++end;
                continue;
            }
            if (source[end++] == quote)
                break;
        }
        return SyntaxToken{{offset, end}, SyntaxKind::String};
    }
    if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_' || first >= 0x80) {
        while (end < source.size()) {
            const unsigned char ch = static_cast<unsigned char>(source[end]);
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '_' || ch >= 0x80)
                ++end;
            else
                break;
        }
        const auto word = source.substr(offset, end - offset);
        const auto kind = samatKeywords().contains(word) ? SyntaxKind::Keyword
                          : samatBuiltins().contains(word) ? SyntaxKind::Builtin
                                                           : SyntaxKind::Identifier;
        return SyntaxToken{{offset, end}, kind};
    }
    if (first >= '0' && first <= '9') {
        if (source.substr(offset, 2) == "0x" || source.substr(offset, 2) == "0X") {
            end = offset + 2;
            while (end < source.size() &&
                   (std::isxdigit(static_cast<unsigned char>(source[end])) || source[end] == '_'))
                ++end;
            return SyntaxToken{{offset, end}, SyntaxKind::Number};
        }
        if (source.substr(offset, 2) == "0b" || source.substr(offset, 2) == "0B") {
            end = offset + 2;
            while (end < source.size() && (source[end] == '0' || source[end] == '1' || source[end] == '_'))
                ++end;
            return SyntaxToken{{offset, end}, SyntaxKind::Number};
        }
        while (end < source.size()) {
            const unsigned char ch = static_cast<unsigned char>(source[end]);
            if ((ch >= '0' && ch <= '9') || ch == '_')
                ++end;
            else if (ch == '.' && end + 1 < source.size() && source[end + 1] != '.')
                ++end;
            else
                break;
        }
        return SyntaxToken{{offset, end}, SyntaxKind::Number};
    }
    static constexpr std::string_view pairs[]{"==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=",
                                               "**", "<<", ">>", "??", "?.", "->", "..", "&&", "||"};
    for (const auto pair : pairs)
        if (source.substr(offset, pair.size()) == pair)
            return SyntaxToken{{offset, offset + pair.size()}, SyntaxKind::Operator};
    return SyntaxToken{{offset, end}, SyntaxKind::Operator};
}

std::optional<TextRange> inputKeyStringAt(std::string_view source, std::size_t cursor) {
    return findInputKeyString(source, cursor);
}

bool replaceInputKeyString(std::string &source, std::size_t cursor, std::string_view keyName) {
    if (!validKeyName(keyName))
        return false;
    const auto range = inputKeyStringAt(source, cursor);
    if (!range)
        return false;
    source.replace(range->begin, range->end - range->begin, keyName);
    return true;
}

bool loadScriptFile(const std::filesystem::path &path, std::string &source, std::string &error) {
    error.clear();
    if (!hasScriptExtension(path)) {
        error = "Samat 소스 파일은 .st 확장자를 사용해야 합니다.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "파일을 열 수 없습니다: " + path.string();
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximumScriptBytes) {
        error = "소스 파일은 16 MiB 이하여야 합니다.";
        return false;
    }
    std::string candidate(static_cast<std::size_t>(size), '\0');
    input.seekg(0, std::ios::beg);
    if (!candidate.empty())
        input.read(candidate.data(), static_cast<std::streamsize>(candidate.size()));
    if (!input && !candidate.empty()) {
        error = "파일을 읽는 중 오류가 발생했습니다: " + path.string();
        return false;
    }
    source = std::move(candidate);
    return true;
}

bool saveScriptFile(const std::filesystem::path &path, std::string_view source, std::string &error) {
    error.clear();
    if (!hasScriptExtension(path)) {
        error = "Samat 소스 파일은 .st 확장자를 사용해야 합니다.";
        return false;
    }
    if (source.size() > maximumScriptBytes) {
        error = "소스 파일은 16 MiB 이하여야 합니다.";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "파일을 저장할 수 없습니다: " + path.string();
        return false;
    }
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!output) {
        error = "파일을 기록하는 중 오류가 발생했습니다: " + path.string();
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> chooseOpenScriptFile(const std::filesystem::path &initialPath) {
#ifdef _WIN32
    std::array<wchar_t, 32768> selected{};
    if (!initialPath.empty()) {
        const auto encoded = initialPath.native();
        const auto count = std::min(encoded.size(), selected.size() - 1);
        std::copy_n(encoded.data(), count, selected.data());
    }
    static constexpr wchar_t filter[] = L"Samat source (*.st)\0*.st\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrDefExt = L"st";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog))
        return std::filesystem::path(selected.data());
#else
    (void)initialPath;
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> chooseOpenHaeryeFile(const std::filesystem::path &initialPath) {
#ifdef _WIN32
    std::array<wchar_t, 32768> selected{};
    if (!initialPath.empty()) {
        const auto encoded = initialPath.native();
        const auto count = std::min(encoded.size(), selected.size() - 1);
        std::copy_n(encoded.data(), count, selected.data());
    }
    static constexpr wchar_t filter[] = L"Haerye scene (*.hy)\0*.hy\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrDefExt = L"hy";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog))
        return std::filesystem::path(selected.data());
#else
    (void)initialPath;
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> chooseSaveScriptFile(const std::filesystem::path &initialPath) {
#ifdef _WIN32
    std::array<wchar_t, 32768> selected{};
    if (!initialPath.empty()) {
        const auto encoded = initialPath.native();
        const auto count = std::min(encoded.size(), selected.size() - 1);
        std::copy_n(encoded.data(), count, selected.data());
    }
    static constexpr wchar_t filter[] = L"Samat source (*.st)\0*.st\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrDefExt = L"st";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&dialog))
        return std::filesystem::path(selected.data());
#else
    (void)initialPath;
#endif
    return std::nullopt;
}

InterpreterRun::~InterpreterRun() { stop(); }

bool InterpreterRun::start(script::Program program, script::RunOptions options) {
    std::lock_guard lifecycleLock(lifecycleMutex_);
    if (worker_.joinable()) {
        bool currentlyRunning = false;
        if (state_) {
            std::lock_guard stateLock(state_->mutex);
            currentlyRunning = state_->snapshot.running;
        }
        if (currentlyRunning)
            return false;
        worker_.join();
    }
    state_ = std::make_shared<State>();
    {
        std::lock_guard stateLock(state_->mutex);
        state_->snapshot.generation = nextGeneration_++;
        state_->snapshot.running = true;
    }
    auto state = state_;
    auto previousOutput = std::move(options.onOutput);
    options.onOutput = [state, previousOutput = std::move(previousOutput)](std::string_view line) {
        {
            std::lock_guard lock(state->mutex);
            state->snapshot.output.emplace_back(line);
            state->pendingOutput.emplace_back(line);
        }
        if (previousOutput)
            previousOutput(line);
    };
    worker_ = std::jthread([state = std::move(state), program = std::move(program),
                            options = std::move(options)](std::stop_token token) mutable {
        auto previousStop = std::move(options.shouldStop);
        options.shouldStop = [token, previousStop = std::move(previousStop)] {
            return token.stop_requested() || (previousStop && previousStop());
        };
        try {
            auto result = script::execute(program, options);
            std::lock_guard lock(state->mutex);
            state->snapshot.output = std::move(result.output);
            state->snapshot.returnValue = std::move(result.returnValue);
            state->snapshot.instructionsExecuted = result.instructionsExecuted;
        } catch (const std::exception &exception) {
            std::lock_guard lock(state->mutex);
            if (!token.stop_requested())
                state->snapshot.error = exception.what();
        } catch (...) {
            std::lock_guard lock(state->mutex);
            if (!token.stop_requested())
                state->snapshot.error = "Unknown interpreter failure.";
        }
        std::lock_guard lock(state->mutex);
        state->snapshot.cancelled = token.stop_requested();
        state->snapshot.running = false;
        state->snapshot.completed = true;
    });
    return true;
}

void InterpreterRun::requestStop() {
    std::lock_guard lock(lifecycleMutex_);
    if (worker_.joinable())
        worker_.request_stop();
}

void InterpreterRun::joinCompleted() {
    std::lock_guard lock(lifecycleMutex_);
    bool completed = false;
    if (state_) {
        std::lock_guard stateLock(state_->mutex);
        completed = state_->snapshot.completed;
    }
    if (completed && worker_.joinable())
        worker_.join();
}

void InterpreterRun::stop() {
    std::lock_guard lock(lifecycleMutex_);
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

std::vector<std::string> InterpreterRun::drainOutput() {
    std::shared_ptr<State> state;
    {
        std::lock_guard lock(lifecycleMutex_);
        state = state_;
    }
    if (!state)
        return {};
    std::lock_guard lock(state->mutex);
    std::vector<std::string> result;
    result.swap(state->pendingOutput);
    return result;
}

RunSnapshot InterpreterRun::snapshot(bool includeOutput) const {
    std::shared_ptr<State> state;
    {
        std::lock_guard lock(lifecycleMutex_);
        state = state_;
    }
    if (!state)
        return {};
    std::lock_guard lock(state->mutex);
    RunSnapshot result;
    result.generation = state->snapshot.generation;
    result.running = state->snapshot.running;
    result.completed = state->snapshot.completed;
    result.cancelled = state->snapshot.cancelled;
    result.instructionsExecuted = state->snapshot.instructionsExecuted;
    if (includeOutput)
        result.output = state->snapshot.output;
    result.returnValue = state->snapshot.returnValue;
    result.error = state->snapshot.error;
    return result;
}

} // namespace jm::studio
