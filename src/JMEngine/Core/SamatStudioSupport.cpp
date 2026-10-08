#include "JMEngine/Core/SamatStudioSupport.hpp"

#include <algorithm>
#include <fstream>
#include <regex>

namespace jm::studio {
namespace {
constexpr std::size_t maximumScriptBytes = 16 * 1024 * 1024;

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

InterpreterRun::~InterpreterRun() { stop(); }

bool InterpreterRun::start(script::Program program, script::RunOptions options) {
    stop();
    std::lock_guard lifecycleLock(lifecycleMutex_);
    state_ = std::make_shared<State>();
    {
        std::lock_guard stateLock(state_->mutex);
        state_->snapshot.generation = nextGeneration_++;
        state_->snapshot.running = true;
    }
    auto state = state_;
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
