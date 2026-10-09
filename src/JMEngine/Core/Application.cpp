#include "JMEngine/Core/Application.hpp"

#include "JMEngine/Project/ProjectStore.hpp"
#include "JMEngine/Scene/Haerye.hpp"
#include "JMEngine/Renderer/GLApi.hpp"
#include "JMEngine/Renderer/Renderer.hpp"
#include "JMEngine/Renderer/Viewport.hpp"
#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/StandardLibrary.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <misc/cpp/imgui_stdlib.h>
#include <TextEditor.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jm {
namespace {

std::string pathToUtf8(const std::filesystem::path &path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

std::filesystem::path utf8ToPath(const std::string &value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path{encoded};
}

std::string scriptKeyName(SDL_Scancode scancode) {
    std::string name = SDL_GetScancodeName(scancode);
    for (char &character : name) {
        if (character == ' ')
            character = '_';
        else
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return name;
}

std::string diagnosticText(const ScriptDiagnostic &diagnostic) {
    return diagnostic.line > 0 ? "줄 " + std::to_string(diagnostic.line) + ": " + diagnostic.message
                               : diagnostic.message;
}

bool tokenizeSamat(const char *begin, const char *end, const char *&tokenBegin, const char *&tokenEnd,
                   TextEditor::PaletteIndex &color) {
    const std::string_view remaining(begin, static_cast<std::size_t>(end - begin));
    const auto token = studio::syntaxTokenAt(remaining, 0);
    if (!token)
        return false;
    tokenBegin = begin;
    tokenEnd = begin + token->range.end;
    switch (token->kind) {
    case studio::SyntaxKind::Keyword: color = TextEditor::PaletteIndex::Keyword; break;
    case studio::SyntaxKind::Builtin: color = TextEditor::PaletteIndex::KnownIdentifier; break;
    case studio::SyntaxKind::Number: color = TextEditor::PaletteIndex::Number; break;
    case studio::SyntaxKind::String: color = *begin == '\'' ? TextEditor::PaletteIndex::CharLiteral
                                                            : TextEditor::PaletteIndex::String; break;
    case studio::SyntaxKind::Comment: color = TextEditor::PaletteIndex::Comment; break;
    case studio::SyntaxKind::Identifier: color = TextEditor::PaletteIndex::Identifier; break;
    case studio::SyntaxKind::Operator: color = TextEditor::PaletteIndex::Punctuation; break;
    }
    return true;
}

const TextEditor::LanguageDefinition &samatLanguageDefinition() {
    static const TextEditor::LanguageDefinition definition = [] {
        TextEditor::LanguageDefinition language;
        language.mName = "Samat / 訓C正音";
        language.mSingleLineComment = "#";
        language.mAutoIndentation = true;
        language.mTokenize = tokenizeSamat;
        return language;
    }();
    return definition;
}

struct LanguageCompletionContext {
    std::vector<std::string> words;
    std::vector<std::string> matches;
    std::unordered_map<std::string, std::string> descriptions;
    std::string prefix;
    int wordStart{0};
    int wordEnd{0};
    int cursor{0};
};

void addCompletionSymbol(LanguageCompletionContext &context, const std::string &name,
                         const std::string &description = {}) {
    if (name.empty())
        return;
    context.words.push_back(name);
    if (!description.empty())
        context.descriptions[name] = description;
}

void collectExpressionCompletions(const script::ExpressionPtr &expression,
                                  LanguageCompletionContext &context) {
    if (!expression)
        return;
    if (expression->kind == script::Expression::Kind::Identifier)
        addCompletionSymbol(context, expression->text, "현재 파일에서 사용한 이름입니다.");
    if (expression->kind == script::Expression::Kind::Member)
        addCompletionSymbol(context, expression->text.starts_with('?') ? expression->text.substr(1)
                                                                        : expression->text,
                            "현재 파일에서 사용한 속성 또는 함수입니다.");
    collectExpressionCompletions(expression->left, context);
    collectExpressionCompletions(expression->right, context);
    for (const auto &element : expression->elements)
        collectExpressionCompletions(element, context);
    for (const auto &[_, element] : expression->entries)
        collectExpressionCompletions(element, context);
    for (const auto &argument : expression->arguments)
        collectExpressionCompletions(argument.value, context);
}

void collectStatementCompletions(const script::StatementList &statements,
                                 LanguageCompletionContext &context) {
    for (const auto &statement : statements) {
        if (statement.kind == script::Statement::Kind::Variable ||
            statement.kind == script::Statement::Kind::Function ||
            statement.kind == script::Statement::Kind::Enum ||
            statement.kind == script::Statement::Kind::Struct ||
            statement.kind == script::Statement::Kind::Import) {
            if (statement.kind == script::Statement::Kind::Function) {
                std::string signature = statement.name + "(";
                for (std::size_t i = 0; i < statement.parameters.size(); ++i) {
                    if (i)
                        signature += ", ";
                    signature += statement.parameters[i];
                    if (i < statement.parameterTypes.size() && statement.parameterTypes[i] != script::Type::Any)
                        signature += ": " + script::annotationName(
                                                   statement.parameterTypes[i],
                                                   i < statement.parameterElementTypes.size()
                                                       ? statement.parameterElementTypes[i]
                                                       : script::Type::Any);
                }
                signature += ")";
                addCompletionSymbol(context, statement.name, "함수 설명 · " + signature);
            } else
                addCompletionSymbol(context, statement.name,
                                    statement.kind == script::Statement::Kind::Import
                                        ? "현재 파일에서 가져오는 모듈입니다."
                                        : "현재 파일에 선언된 이름입니다.");
        }
        for (const auto &parameter : statement.parameters)
            addCompletionSymbol(context, parameter, "함수 매개변수입니다.");
        for (const auto &field : statement.body)
            if (statement.kind == script::Statement::Kind::Enum ||
                statement.kind == script::Statement::Kind::Struct)
                addCompletionSymbol(context, field.name, "자료형의 멤버입니다.");
        collectExpressionCompletions(statement.target, context);
        collectExpressionCompletions(statement.expression, context);
        collectExpressionCompletions(statement.rangeEnd, context);
        collectStatementCompletions(statement.body, context);
        collectStatementCompletions(statement.alternative, context);
    }
}

std::string beginnerDiagnostic(const script::Diagnostic &diagnostic) {
    const auto &message = diagnostic.message;
    if (message.find("Unknown variable") != std::string::npos ||
        message.find("Unknown identifier") != std::string::npos)
        return "이 이름을 찾지 못했어요. 철자를 확인하거나 먼저 변수를 선언해 보세요.";
    if (message.find("Expected a value") != std::string::npos ||
        message.find("Expected expression") != std::string::npos)
        return "여기에 값이나 계산식이 필요해요. 예: print(42)";
    if (message.find("Expected ')' ") != std::string::npos ||
        message.find("Expected ']' ") != std::string::npos ||
        message.find("Expected '}' ") != std::string::npos)
        return "괄호 짝이 맞지 않아요. 여는 괄호와 닫는 괄호를 확인해 보세요.";
    if (message.find("Cannot divide by zero") != std::string::npos)
        return "0으로 나눌 수 없어요. 나누는 값이 0인지 확인해 보세요.";
    if (message.find("requires") != std::string::npos && message.find("arguments") != std::string::npos)
        return "함수에 필요한 입력값 수가 맞지 않아요. 괄호 안의 매개변수를 확인해 보세요.";
    return message;
}

std::size_t editorByteOffset(const std::string &source, TextEditor::Coordinates position) {
    std::size_t offset = 0;
    int line = 0;
    while (line < position.mLine && offset < source.size()) {
        const auto newline = source.find('\n', offset);
        if (newline == std::string::npos)
            return source.size();
        offset = newline + 1;
        ++line;
    }
    int column = 0;
    while (offset < source.size() && source[offset] != '\n' && column < position.mColumn) {
        const unsigned char ch = static_cast<unsigned char>(source[offset]);
        if (ch == '\t') {
            column += 4 - (column % 4);
            ++offset;
        } else {
            std::size_t width = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
            offset = std::min(source.size(), offset + width);
            ++column;
        }
    }
    return offset;
}

TextEditor::Coordinates editorPositionAt(const std::string &source, std::size_t offset) {
    TextEditor::Coordinates position{};
    offset = std::min(offset, source.size());
    for (std::size_t i = 0; i < offset;) {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        if (ch == '\n') {
            ++position.mLine;
            position.mColumn = 0;
            ++i;
        } else if (ch == '\t') {
            position.mColumn += 4 - (position.mColumn % 4);
            ++i;
        } else {
            i += ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
            ++position.mColumn;
        }
    }
    return position;
}

const char *languageStarterCode = "fn add(a, b):\n"
                                  "    return a + b\n\n"
                                  "let result = add(10, 20)\n"
                                  "print(result)\n";

} // namespace

Application::Application(ApplicationConfig config) : config_(std::move(config)) {
    projectRoot_ = std::filesystem::current_path() / "JMProjects" / "My First Game";
    projectPathInput_ = pathToUtf8(projectRoot_);
    ProjectSettings initialProject = ProjectStore::makeNewProject("My First Game");
    projectName_ = initialProject.name;
    projectId_ = initialProject.projectId;
    scene_.replaceObjects({});
    scene_.setName("새 장면");
    script_ = makeDefaultScript(scene_);
    languageCodeBuffer_ = languageStarterCode;
    languageEditor_ = std::make_unique<TextEditor>();
    languageEditor_->SetLanguageDefinition(samatLanguageDefinition());
    languageEditor_->SetTabSize(4);
    languageEditor_->SetText(languageCodeBuffer_);
    languageEditorTextSnapshot_ = languageCodeBuffer_;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string{"SDL initialization failed: "} + SDL_GetError());
    }

    try {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            throw std::runtime_error(std::string{"Window creation failed: "} + SDL_GetError());
        }
        SDL_SetWindowMinimumSize(window_, 1000, 620);

        glContext_ = SDL_GL_CreateContext(window_);
        if (glContext_ == nullptr) {
            throw std::runtime_error(std::string{"OpenGL context creation failed: "} + SDL_GetError());
        }

        if (!SDL_GL_MakeCurrent(window_, glContext_)) {
            throw std::runtime_error(std::string{"Making OpenGL context current failed: "} + SDL_GetError());
        }

        if (!gl::load()) {
            throw std::runtime_error(std::string{"Could not load the OpenGL 3.3 functions: "} +
                                     SDL_GetError());
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContextReady_ = true;
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        const char *koreanFontPath = "C:/Windows/Fonts/malgun.ttf";
        if (std::filesystem::exists(koreanFontPath)) {
            if (ImFont *koreanFont = io.Fonts->AddFontFromFileTTF(koreanFontPath, 17.0F, nullptr,
                                                                  io.Fonts->GetGlyphRangesKorean())) {
                io.FontDefault = koreanFont;
            }
        }
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 10.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.PopupRounding = 8.0F;
    style.ScrollbarRounding = 8.0F;
    style.TabRounding = 6.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.067F, 0.090F, 1.0F);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075F, 0.088F, 0.115F, 1.0F);
    style.Colors[ImGuiCol_Border] = ImVec4(0.15F, 0.19F, 0.24F, 1.0F);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.105F, 0.125F, 0.16F, 1.0F);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14F, 0.18F, 0.22F, 1.0F);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.16F, 0.22F, 0.25F, 1.0F);
    style.Colors[ImGuiCol_Button] = ImVec4(0.11F, 0.16F, 0.19F, 1.0F);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16F, 0.24F, 0.26F, 1.0F);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.12F, 0.32F, 0.29F, 1.0F);
    style.Colors[ImGuiCol_Header] = ImVec4(0.10F, 0.24F, 0.23F, 0.8F);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12F, 0.31F, 0.29F, 0.9F);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.10F, 0.38F, 0.33F, 1.0F);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.38F, 0.88F, 0.72F, 1.0F);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.30F, 0.76F, 0.63F, 1.0F);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42F, 0.92F, 0.76F, 1.0F);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.08F, 0.105F, 0.14F, 1.0F);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.13F, 0.26F, 0.25F, 1.0F);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.10F, 0.20F, 0.20F, 1.0F);
        if (!ImGui_ImplSDL3_InitForOpenGL(window_, glContext_)) {
            throw std::runtime_error("Could not initialize the editor input backend.");
        }
        imguiPlatformReady_ = true;
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
            throw std::runtime_error("Could not initialize the editor renderer.");
        }
        imguiRendererReady_ = true;

        renderer_ = std::make_unique<Renderer>();
        SDL_GL_SetSwapInterval(1);
    } catch (...) {
        shutdown();
        SDL_Quit();
        throw;
    }
}

Application::~Application() {
    shutdown();
    SDL_Quit();
}

void Application::shutdown() noexcept {
    languageRun_.stop();
    if (imguiRendererReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        imguiRendererReady_ = false;
    }
    if (imguiPlatformReady_) {
        ImGui_ImplSDL3_Shutdown();
        imguiPlatformReady_ = false;
    }
    renderer_.reset();
    if (imguiContextReady_) {
        ImGui::DestroyContext();
        imguiContextReady_ = false;
    }
    if (glContext_ != nullptr) {
        SDL_GL_DestroyContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

std::size_t Application::run(std::size_t maximumFrames) {
    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    running_ = true;

    std::size_t frames = 0;
    while (running_ && (!maximumFrames || frames < maximumFrames)) {
        const auto frameStart = Clock::now();
        const auto elapsed = std::chrono::duration<float>(frameStart - previousFrame).count();
        previousFrame = frameStart;
        const float deltaSeconds = elapsed < 0.25F ? elapsed : 0.25F;

        processEvents();
        update(deltaSeconds);
        render();
        if (maximumFrames && glGetError() != GL_NO_ERROR)
            throw std::runtime_error("OpenGL error during sandbox smoke rendering.");
        ++frames;
    }
    return frames;
}

void Application::processEvents() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (imguiPlatformReady_)
            ImGui_ImplSDL3_ProcessEvent(&event);
        const ImGuiIO &io = ImGui::GetIO();
        const bool languageEditorFocused = activeWorkspace_ == 1 && languageEditor_ &&
                                           languageEditor_->IsFocused();
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (languageDirty_) {
                languagePendingDocumentAction_ = 4;
                languageQuitPending_ = true;
                requestedWorkspace_ = 1;
            } else
                running_ = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat)
                break;
            if (!io.WantTextInput && event.key.key == SDLK_F5 && (event.key.mod & SDL_KMOD_SHIFT) == 0)
                togglePlaying();
            if (!io.WantTextInput && event.key.key == SDLK_F5 &&
                (event.key.mod & SDL_KMOD_SHIFT) != 0 && playing_)
                togglePlaying();
            if (event.key.key == SDLK_SPACE && playing_ && !io.WantTextInput)
                spacePressedThisFrame_ = true;
            if (playing_ && !io.WantTextInput)
                keysPressedThisFrame_.insert(scriptKeyName(event.key.scancode));
            if (io.WantCaptureKeyboard || playing_ || languageEditorFocused)
                break;
            if (event.key.key == SDLK_ESCAPE)
                running_ = false;
            if (event.key.key == SDLK_DELETE)
                scene_.deleteSelected();
            if (event.key.key == SDLK_S && (event.key.mod & SDL_KMOD_CTRL) != 0)
                saveProject();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (viewportHovered_ && ((twoDimensional_ && event.button.button == SDL_BUTTON_MIDDLE) ||
                                     (!twoDimensional_ && event.button.button == SDL_BUTTON_LEFT)))
                dragging_ = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT)
                dragging_ = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragging_) {
                if (twoDimensional_) {
                    const float unitsPerPixel = 10.0F / (zoom2D_ * std::max(1.0F, viewportHeight_));
                    cameraPanX2D_ -= event.motion.xrel * unitsPerPixel;
                    cameraPanY2D_ += event.motion.yrel * unitsPerPixel;
                } else {
                    cameraYaw_ += event.motion.xrel * 0.01F;
                    cameraPitch_ += event.motion.yrel * 0.01F;
                    cameraPitch_ = std::clamp(cameraPitch_, -1.45F, 1.45F);
                }
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (io.WantCaptureMouse && !viewportHovered_)
                break;
            if (twoDimensional_) {
                zoom2D_ = std::clamp(zoom2D_ * std::pow(1.12F, event.wheel.y), 0.35F, 3.0F);
            } else {
                cameraDistance_ = std::clamp(cameraDistance_ * std::pow(0.9F, event.wheel.y), 1.7F, 12.0F);
            }
            break;
        default:
            break;
        }
    }
}

void Application::update(float deltaSeconds) {
    if (playing_) {
        for (GameObject &object : scene_.objects()) {
            if (!object.spinWhenPlaying)
                continue;
            if (object.kind == ObjectKind::Cube3D)
                object.rotationDegrees.y += 60.0F * deltaSeconds;
            else
                object.rotationDegrees.z += 60.0F * deltaSeconds;
        }
        constexpr float fixedStep = 1.0F / 60.0F;
        scriptAccumulator_ = std::min(scriptAccumulator_ + std::min(deltaSeconds, 0.1F), 0.1F);
        const bool *input = SDL_GetKeyboardState(nullptr);
        const bool acceptGameInput = activeWorkspace_ == 2 || !ImGui::GetIO().WantTextInput;
        const bool rightHeld = acceptGameInput && input[SDL_SCANCODE_RIGHT];
        const bool leftHeld = acceptGameInput && input[SDL_SCANCODE_LEFT];
        const bool spacePressed = acceptGameInput && spacePressedThisFrame_;
        std::unordered_set<std::string> keysHeld;
        if (acceptGameInput) {
            for (int index = 1; index < SDL_SCANCODE_COUNT; ++index) {
                const auto scancode = static_cast<SDL_Scancode>(index);
                if (input[index])
                    keysHeld.insert(scriptKeyName(scancode));
            }
        }
        int steps = 0;
        while (scriptAccumulator_ >= fixedStep && steps < 6) {
            ScriptInput scriptInput{rightHeld, leftHeld, spacePressed && steps == 0, {}, {}};
            scriptInput.keysHeld = keysHeld;
            if (steps == 0 && acceptGameInput)
                scriptInput.keysPressed = keysPressedThisFrame_;
            if (languageRuntime_) {
                try {
                    languageRuntime_->tick(scriptInput, fixedStep);
                } catch (const std::exception &error) {
                    auto message = std::string(error.what());
                    togglePlaying();
                    languageStatus_ = message;
                    scriptStatus_ = message;
                    break;
                }
            } else
                executeScript(compiledScript_, scene_, scriptInput, fixedStep);
            scene_.stepPhysics2D(fixedStep);
            scriptAccumulator_ -= fixedStep;
            ++steps;
        }
        if (steps == 6)
            scriptAccumulator_ = 0.0F;
        if (steps > 0) {
            spacePressedThisFrame_ = false;
            keysPressedThisFrame_.clear();
        }
    } else {
        scriptAccumulator_ = 0.0F;
        spacePressedThisFrame_ = false;
        keysPressedThisFrame_.clear();
    }

    if (playing_ || ImGui::GetIO().WantCaptureKeyboard)
        return;
}

void Application::set2DMode(bool enabled) {
    if (!playing_)
        requestedWorkspace_ = 0;
    if (twoDimensional_ == enabled)
        return;
    twoDimensional_ = enabled;
    scene_.selectFirst(enabled ? ObjectKind::Sprite2D : ObjectKind::Cube3D);
    SDL_SetWindowTitle(window_, "Samat Studio");
}

void Application::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawEditorUI();

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GetWindowSizeInPixels(window_, &drawableWidth, &drawableHeight);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.035F, 0.045F, 0.070F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (viewportVisible_) {
        const PixelViewport viewport = scaleViewportToPixels(
            viewportX_, viewportY_, viewportWidth_, viewportHeight_, ImGui::GetIO().DisplaySize.x,
            ImGui::GetIO().DisplaySize.y, drawableWidth, drawableHeight);
        renderer_->drawScene(drawableWidth, drawableHeight, viewport.x, viewport.yFromTop, viewport.width,
                             viewport.height, twoDimensional_, cameraYaw_, cameraPitch_, cameraDistance_,
                             cameraPanX2D_, cameraPanY2D_, zoom2D_, scene_.objects(), scene_.backgroundColor());
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

void Application::drawEditorUI() {
    const ImGuiIO &io = ImGui::GetIO();
    const ObjectKind activeKind = twoDimensional_ ? ObjectKind::Sprite2D : ObjectKind::Cube3D;
    viewportVisible_ = false;
    viewportHovered_ = false;

    const float toolbarWidth = std::max(700.0F, io.DisplaySize.x - 28.0F);
    const ImGuiWindowFlags fixedPanel = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::SetNextWindowPos(ImVec2(14.0F, 12.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbarWidth, 88.0F), ImGuiCond_Always);
    if (ImGui::Begin("##SamatStudioHeader", nullptr, fixedPanel | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(ImVec4(0.40F, 0.91F, 0.75F, 1.0F), "SAMAT");
        ImGui::SameLine();
        ImGui::Text("Studio");
        ImGui::SameLine();
        ImGui::TextDisabled("· 코드와 장면을 함께 만드는 공간");
        ImGui::SameLine(0.0F, 26.0F);
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("장면 열기", ImVec2(112.0F, 30.0F))) {
            const auto initial = haeryeFilePath_.empty() ? std::filesystem::path{}
                                                         : utf8ToPath(haeryeFilePath_);
            if (const auto selected = studio::chooseOpenHaeryeFile(initial)) {
                std::ifstream input(*selected, std::ios::binary);
                if (input) {
                    std::string source(std::istreambuf_iterator<char>(input), {});
                    try {
                        loadHaerye(std::move(source));
                        haeryeFilePath_ = pathToUtf8(*selected);
                        activeWorkspace_ = 0;
                        requestedWorkspace_ = 0;
                    } catch (const std::exception &error) {
                        scriptStatus_ = std::string{"장면을 열지 못했어요 · "} + error.what();
                    }
                } else {
                    scriptStatus_ = "Haerye 장면 파일을 열 수 없습니다.";
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Samat 코드 열기", ImVec2(136.0F, 30.0F))) {
            const auto initial = languageFilePath_.empty() ? std::filesystem::path{}
                                                           : utf8ToPath(languageFilePath_);
            if (const auto selected = studio::chooseOpenScriptFile(initial)) {
                languagePendingDocumentAction_ = 2;
                languagePendingFilePath_ = pathToUtf8(*selected);
                languageExternalOpenRequested_ = true;
                requestedWorkspace_ = 1;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!playing_ && !languagePlayEnabled_);
        ImGui::PushStyleColor(ImGuiCol_Button, playing_ ? ImVec4(0.40F, 0.19F, 0.20F, 1.0F)
                                                       : ImVec4(0.10F, 0.40F, 0.33F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing_ ? ImVec4(0.53F, 0.22F, 0.24F, 1.0F)
                                                              : ImVec4(0.13F, 0.50F, 0.41F, 1.0F));
        if (ImGui::Button(playing_ ? "■  정지" : "▶  실행", ImVec2(104.0F, 30.0F)))
            togglePlaying();
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("F5");
        ImGui::Separator();
        const std::string sceneLabel = haeryeFilePath_.empty()
                                           ? "장면을 열어 시작하세요"
                                           : pathToUtf8(utf8ToPath(haeryeFilePath_).filename());
        const std::string languageLabel = languageFilePath_.empty()
                                              ? "Samat 코드 미선택"
                                              : pathToUtf8(utf8ToPath(languageFilePath_).filename());
        ImGui::TextColored(ImVec4(0.50F, 0.76F, 0.78F, 1.0F), "HAERYE");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", sceneLabel.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("  /  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.62F, 0.73F, 0.98F, 1.0F), "SAMAT");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", languageLabel.c_str());
        if (playing_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.40F, 0.91F, 0.75F, 1.0F), "실행 중");
        }
    }
    ImGui::End();

    const bool focusedWorkspace = activeWorkspace_ != 0;
    const float workspaceX = focusedWorkspace ? 10.0F : 250.0F;
    const float workspaceWidth = std::max(300.0F, io.DisplaySize.x - (focusedWorkspace ? 20.0F : 550.0F));
    ImGui::SetNextWindowPos(ImVec2(workspaceX, 112.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workspaceWidth, std::max(250.0F, io.DisplaySize.y - 126.0F)),
                             ImGuiCond_Always);
    if (ImGui::Begin("##SamatStudioWorkspace", nullptr,
                     fixedPanel | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::BeginTabBar("SamatStudioWorkspaces")) {
            if (ImGui::BeginTabItem("장면", nullptr,
                                    requestedWorkspace_ == 0 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 0;
                ImGui::TextDisabled("장면 미리보기  ·  가운데 버튼으로 이동  ·  휠로 확대/축소");
                if (!haeryeFilePath_.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("· 속성 변경은 현재 미리보기에 적용됩니다");
                }
                if (scene_.objects().empty())
                    ImGui::TextDisabled("Haerye 장면을 열거나 왼쪽에서 오브젝트를 추가하세요.");
                captureViewport();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Samat 코드", nullptr,
                                    requestedWorkspace_ == 1 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 1;
                drawCodePanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("미리보기", nullptr,
                                    requestedWorkspace_ == 2 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 2;
                ImGui::TextDisabled(playing_ ? "게임 실행 중 · 입력을 눌러 확인하세요"
                                             : "장면과 Samat 코드를 함께 확인하는 미리보기");
                captureViewport();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            requestedWorkspace_ = -1;
        }
    }
    ImGui::End();

    if (activeWorkspace_ == 0) {
        ImGui::SetNextWindowPos(ImVec2(14.0F, 112.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(218.0F, std::max(250.0F, io.DisplaySize.y - 126.0F)),
                                 ImGuiCond_Always);
        if (ImGui::Begin("장면 오브젝트", nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled("HAERYE SCENE");
            if (ImGui::Button("+ 오브젝트", ImVec2(-1.0F, 30.0F)))
                scene_.create(ObjectKind::Sprite2D);
            ImGui::Separator();
            if (ImGui::BeginChild("Scene objects", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders)) {
                for (const GameObject &object : scene_.objects()) {
                    if (object.kind != activeKind)
                        continue;
                    const GameObject *selected = scene_.selected();
                    ImGui::PushID(object.id.c_str());
                    if (ImGui::Selectable(object.name.c_str(),
                                          selected != nullptr && selected->id == object.id)) {
                        scene_.select(object.id);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 264.0F, 112.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(250.0F, std::max(250.0F, io.DisplaySize.y - 126.0F)),
                                 ImGuiCond_Always);
        if (ImGui::Begin("선택 항목", nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            GameObject *object = scene_.selected();
            if (object == nullptr || object->kind != activeKind) {
                ImGui::TextDisabled("왼쪽 목록에서 오브젝트를 선택하세요.");
            } else {
                ImGui::TextDisabled("HAERYE OBJECT");
                ImGui::InputText("이름", &object->name);
                ImGui::SeparatorText("변환");
                ImGui::DragFloat2("위치", &object->position.x, 0.05F, -100.0F, 100.0F, "%.2f");
                ImGui::DragFloat("회전", &object->rotationDegrees.z, 1.0F, -360.0F, 360.0F, "%.0f°");
                ImGui::DragFloat2("크기", &object->scale.x, 0.02F, 0.05F, 20.0F, "%.2f");
                ImGui::SeparatorText("모양");
                ImGui::ColorEdit3("색상", &object->color.x);
                if (ImGui::Button("오브젝트 삭제", ImVec2(-1.0F, 30.0F)))
                    scene_.deleteSelected();
            }
        }
        ImGui::End();
    }
}

void Application::drawCodePanel() {
    drawLanguageCorePanel();
}

void Application::drawLegacyCodePanel() {
    if (!codeMode_) {
        ImGui::TextDisabled(twoDimensional_ ? "훈기정음 · 2D Samat AST"
                                            : "훈기정음 · 게임 실행은 2D 모드에서 지원");
        ImGui::SameLine();
        if (ImGui::Button(koreanEditMode_ ? "한글 문장 적용" : "한글 문장 편집")) {
            if (!koreanEditMode_) {
                koreanBuffer_ = formatScriptKorean(script_, scene_);
                koreanEditMode_ = true;
                tutorialCodeViewed_ = true;
            } else {
                ScriptDiagnostic diagnostic;
                ScriptDocument parsed = script_;
                if (parseScriptKorean(koreanBuffer_, scene_, parsed, diagnostic)) {
                    script_ = std::move(parsed);
                    koreanEditMode_ = false;
                    scriptStatus_ = "한글 문장을 같은 Samat AST에 적용했어요.";
                } else {
                    scriptStatus_ = diagnosticText(diagnostic);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("코드 모드로 보기")) {
            codeBuffer_ = formatScriptCode(script_, scene_);
            codeMode_ = true;
            koreanEditMode_ = false;
            tutorialCodeViewed_ = true;
            scriptStatus_.clear();
        }
        const GameObject *target = nullptr;
        for (const GameObject &object : scene_.objects())
            if (object.name == "Player" && object.kind == ObjectKind::Sprite2D) {
                target = &object;
                break;
            }
        if (target == nullptr)
            for (const GameObject &object : scene_.objects())
                if (object.kind == ObjectKind::Sprite2D && !object.isStatic) {
                    target = &object;
                    break;
                }
        if (target != nullptr) {
            for (ScriptEventHandler &handler : script_.handlers) {
                for (ScriptStatement &statement : handler.body) {
                    if (statement.action == ScriptAction::SetMovementSpeed ||
                        statement.action == ScriptAction::MoveHorizontal ||
                        statement.action == ScriptAction::Jump)
                        statement.targetObjectId = target->id;
                    if (statement.action == ScriptAction::SetMovementSpeed)
                        ImGui::DragFloat("시작 속도", &statement.value, 0.1F, 0.0F, 100.0F, "%.1f");
                    if (statement.action == ScriptAction::Jump)
                        ImGui::DragFloat("점프 힘", &statement.value, 0.1F, 0.1F, 100.0F, "%.1f");
                }
            }
        }
        ImGui::Spacing();
        if (koreanEditMode_) {
            const float editorHeight = std::max(150.0F, ImGui::GetContentRegionAvail().y - 160.0F);
            ImGui::InputTextMultiline("##jmkorean", &koreanBuffer_, ImVec2(-1.0F, editorHeight));
        } else {
            ImGui::TextWrapped("%s", formatScriptKorean(script_, scene_).c_str());
        }
    } else {
        ImGui::TextDisabled(twoDimensional_ ? "JM Code · 2D game script" : "JM Code · switch to 2D to run");
        ImGui::SameLine();
        if (ImGui::SmallButton("+ 숫자 변수")) {
            int suffix = 1;
            std::string name;
            do {
                name = "newNumber" + std::to_string(suffix++);
            } while (codeBuffer_.find("let " + name) != std::string::npos ||
                     codeBuffer_.find("const " + name) != std::string::npos);
            codeBuffer_.insert(0, "let " + name + " = 1\n");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ 함수")) {
            int suffix = 1;
            std::string name;
            do {
                name = "myFunction" + std::to_string(suffix++);
            } while (codeBuffer_.find("fn " + name + "(") != std::string::npos);
            const std::size_t eventPosition = codeBuffer_.find("on ");
            const std::string snippet = "fn " + name + "():\n    # 여기에서 동작을 작성하세요.\n\n";
            codeBuffer_.insert(eventPosition == std::string::npos ? codeBuffer_.size() : eventPosition,
                               snippet);
        }
        ImGui::SameLine();
        if (ImGui::Button("한글 모드로 적용")) {
            ScriptDiagnostic diagnostic;
            ScriptDocument parsed = script_;
            if (parseScriptCode(codeBuffer_, scene_, parsed, diagnostic)) {
                script_ = std::move(parsed);
                codeMode_ = false;
                koreanEditMode_ = false;
                scriptStatus_ = "코드를 같은 스크립트 AST에 적용했어요.";
            } else {
                scriptStatus_ = diagnosticText(diagnostic);
            }
        }
        const float editorHeight = std::max(160.0F, ImGui::GetContentRegionAvail().y - 70.0F);
        ImGui::InputTextMultiline("##samat", &codeBuffer_, ImVec2(-1.0F, editorHeight));
    }

    ImGui::SeparatorText("첫 게임 만들기");
    const auto checklist = [](bool done, const char *label) {
        ImGui::TextColored(done ? ImVec4(0.30F, 0.85F, 0.55F, 1.0F) : ImVec4(0.70F, 0.72F, 0.78F, 1.0F),
                           "%s %s", done ? "✓" : "○", label);
    };
    checklist(std::any_of(scene_.objects().begin(), scene_.objects().end(),
                          [](const GameObject &item) {
                              return item.name == "Player" && item.kind == ObjectKind::Sprite2D;
                          }),
              "장면에 Player가 있어요");
    checklist(script_.handlers.size() >= 4, "좌우 이동과 점프가 준비됐어요");
    checklist(tutorialCodeViewed_, "코드 보기 전환해 보기");
    checklist(tutorialPlayRun_, "실행해서 직접 움직여 보기");

    if (!scriptStatus_.empty())
        ImGui::TextWrapped("%s", scriptStatus_.c_str());
}

void Application::drawLanguageCorePanel() {
    using namespace script;
    for (const auto &line : languageRun_.drainOutput())
        languageOutput_ += line + '\n';
    auto runAtFrameStart = languageRun_.snapshot(false);
    if (runAtFrameStart.completed && runAtFrameStart.generation > languageLastRunGeneration_) {
        runAtFrameStart = languageRun_.snapshot();
        languageLastRunGeneration_ = runAtFrameStart.generation;
        languageCompiled_ = runAtFrameStart.error.empty();
        languageErrorLine_ = 0;
        languageDiagnostic_ = runAtFrameStart.error;
        if (runAtFrameStart.cancelled)
            languageStatus_ = "실행을 정지했어요.";
        else if (!runAtFrameStart.error.empty())
            languageStatus_ = "실행 오류 · " + runAtFrameStart.error;
        else
            languageStatus_ = "실행 완료 · " + std::to_string(runAtFrameStart.instructionsExecuted) +
                              "개 명령 처리";
        languageOutput_.clear();
        for (const auto &line : runAtFrameStart.output)
            languageOutput_ += line + '\n';
        if (!runAtFrameStart.returnValue.isNull())
            languageOutput_ += "결과: " + runAtFrameStart.returnValue.toString() + '\n';
    }
    languageRun_.joinCompleted();
    auto resetDocument = [&] {
        languageCodeBuffer_ = languageStarterCode;
        languageFilePath_.clear();
        languagePathInput_.clear();
        languageKoreanSyntax_ = false;
        languageDirty_ = false;
        languageErrorLine_ = 0;
        languageOutput_.clear();
        languageDiagnostic_.clear();
        languageStatus_ = "새 Samat 문서를 만들었어요.";
    };
    auto openDocument = [&](const std::string &requestedPath) {
        if (requestedPath.empty()) {
            languageStatus_ = ".st 파일 경로를 입력해 주세요.";
            return false;
        }
        std::string source, error;
        if (!studio::loadScriptFile(utf8ToPath(requestedPath), source, error)) {
            languageStatus_ = error;
            return false;
        }
        Program candidate;
        Diagnostic codeDiagnostic, koreanDiagnostic;
        const bool codeParsed = parseCode(source, candidate, codeDiagnostic);
        const bool koreanParsed = codeParsed || parseKorean(source, candidate, koreanDiagnostic);
        if (codeParsed)
            languageKoreanSyntax_ = false;
        else if (koreanParsed)
            languageKoreanSyntax_ = true;
        else
            languageKoreanSyntax_ = source.find("함수 ") != std::string::npos ||
                                    source.find("변수 ") != std::string::npos ||
                                    source.find("정한다") != std::string::npos;
        languageCodeBuffer_ = std::move(source);
        languageFilePath_ = requestedPath;
        languagePathInput_ = requestedPath;
        languagePlayEnabled_ = true;
        languageDirty_ = false;
        const auto &parseDiagnostic = codeParsed ? codeDiagnostic : koreanDiagnostic;
        languageErrorLine_ = codeParsed || koreanParsed ? 0 : parseDiagnostic.line;
        languageDiagnostic_ = codeParsed || koreanParsed ? std::string{} : parseDiagnostic.message;
        languageOutput_.clear();
        languageStatus_ = codeParsed || koreanParsed
                              ? "파일을 열었어요. 문법이 맞지 않아도 편집할 수 있습니다."
                              : languageBeginnerMode_ ? beginnerDiagnostic(parseDiagnostic)
                                                      : parseDiagnostic.message;
        return true;
    };
    auto saveDocument = [&](bool saveAs = false, bool allowPathInput = true) {
        const std::string targetPath = saveAs || (languageFilePath_.empty() && allowPathInput)
                                           ? languagePathInput_
                                           : languageFilePath_;
        if (targetPath.empty()) {
            languageStatus_ = "저장할 .st 파일 경로를 입력해 주세요.";
            return false;
        }
        std::string error;
        if (!studio::saveScriptFile(utf8ToPath(targetPath), languageCodeBuffer_, error)) {
            languageStatus_ = error;
            return false;
        }
        languageFilePath_ = targetPath;
        languagePathInput_ = targetPath;
        languageDirty_ = false;
        languageStatus_ = "저장했어요 · " + targetPath;
        return true;
    };
    auto performDocumentAction = [&] {
        if (languagePendingDocumentAction_ == 1)
            resetDocument();
        else if (languagePendingDocumentAction_ == 2)
            openDocument(languagePendingFilePath_);
        else if (languagePendingDocumentAction_ == 3) {
            if (openDocument(languagePendingFilePath_)) {
                languageFilePath_.clear();
                languagePathInput_.clear();
                languageStatus_ = "예제를 새 문서로 열었어요. 저장할 때 위치와 이름을 정해 주세요.";
            }
        } else if (languagePendingDocumentAction_ == 4) {
            languageQuitPending_ = false;
            running_ = false;
        }
        languagePendingFilePath_.clear();
        languagePendingDocumentAction_ = 0;
    };
    if (languageExternalOpenRequested_) {
        languageExternalOpenRequested_ = false;
        if (languageDirty_)
            ImGui::OpenPopup("저장하지 않은 변경사항");
        else
            performDocumentAction();
    }
    ImGui::TextUnformatted("Samat Studio");
    ImGui::SameLine();
    ImGui::Checkbox("초보자 모드", &languageBeginnerMode_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("도움말과 쉬운 오류 설명만 바꿉니다. 언어 기능과 실행 방식은 같습니다.");
    ImGui::SetNextItemWidth(std::max(180.0F, ImGui::GetContentRegionAvail().x - 260.0F));
    ImGui::InputText("파일 경로 (.st)", &languagePathInput_);
    ImGui::SameLine();
    if (ImGui::SmallButton("새로 만들기")) {
        languagePendingDocumentAction_ = 1;
        if (languageDirty_)
            ImGui::OpenPopup("저장하지 않은 변경사항");
        else
            performDocumentAction();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("열기")) {
        languagePendingDocumentAction_ = 2;
#ifdef _WIN32
        if (const auto selected = studio::chooseOpenScriptFile(
                languagePathInput_.empty() ? std::filesystem::path{} : utf8ToPath(languagePathInput_)))
            languagePendingFilePath_ = pathToUtf8(*selected);
        else
            languagePendingDocumentAction_ = 0;
#else
        languagePendingFilePath_ = languagePathInput_;
#endif
        if (languagePendingDocumentAction_ != 0 && languageDirty_)
            ImGui::OpenPopup("저장하지 않은 변경사항");
        else if (languagePendingDocumentAction_ != 0)
            performDocumentAction();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("저장"))
        saveDocument();
    ImGui::SameLine();
    if (ImGui::SmallButton("다른 이름으로 저장")) {
#ifdef _WIN32
        if (const auto selected = studio::chooseSaveScriptFile(
                languagePathInput_.empty() ? std::filesystem::path{} : utf8ToPath(languagePathInput_))) {
            languagePathInput_ = pathToUtf8(*selected);
            saveDocument(true);
        }
#else
        saveDocument(true);
 #endif
    }
    if (languageQuitPending_)
        ImGui::OpenPopup("저장하지 않은 변경사항");
    if (ImGui::BeginPopupModal("저장하지 않은 변경사항", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(languagePendingDocumentAction_ == 4
                                   ? "종료하기 전에 저장하지 않은 변경사항이 있어요."
                                   : "현재 소스에 저장하지 않은 변경사항이 있어요.");
        if (languageFilePath_.empty())
            ImGui::TextUnformatted("새 문서는 취소 후 다른 이름으로 저장한 다음 작업을 이어가세요.");
        if (ImGui::Button(languagePendingDocumentAction_ == 4 ? "저장 후 종료" : "저장 후 계속")) {
            if (saveDocument(false, false)) {
                performDocumentAction();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(languagePendingDocumentAction_ == 4 ? "버리고 종료" : "버리고 계속")) {
            performDocumentAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("취소")) {
            languagePendingDocumentAction_ = 0;
            languagePendingFilePath_.clear();
            languageQuitPending_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("현재 문서: %s", languageFilePath_.empty() ? "새 문서" : languageFilePath_.c_str());
    if (ImGui::SmallButton(languageKoreanSyntax_ ? "문법: 訓C正音" : "Syntax: Code")) {
        Program program;
        Diagnostic diagnostic;
        if ((languageKoreanSyntax_ ? parseKorean(languageCodeBuffer_, program, diagnostic)
                                   : parseCode(languageCodeBuffer_, program, diagnostic))) {
            try {
                languageCodeBuffer_ = languageKoreanSyntax_ ? renderCode(program) : renderKorean(program);
                languageKoreanSyntax_ = !languageKoreanSyntax_;
                languageDirty_ = true;
                languageErrorLine_ = 0;
                languageStatus_ = "같은 AST의 구문 표현을 변환했어요.";
            } catch (const std::exception &error) {
                languageStatus_ = error.what();
            }
        } else {
            languageErrorLine_ = diagnostic.line;
            languageDiagnostic_ = diagnostic.message;
            languageStatus_ = languageBeginnerMode_ ? beginnerDiagnostic(diagnostic) : diagnostic.message;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Code / 訓C正音  ·  하나의 실행 구조");
    auto parseEditorProgram = [&](Program &program, Diagnostic &diagnostic) {
        return languageKoreanSyntax_ ? parseKorean(languageCodeBuffer_, program, diagnostic)
                                     : parseCode(languageCodeBuffer_, program, diagnostic);
    };
    ImGui::TextDisabled("Samat 소스 편집 · 게임 장면 실행은 상단 ▶ 실행 · 독립 코드는 Interpreter로 실행");
    ImGui::SameLine();
    if (ImGui::BeginCombo("예제 선택", "Samat v1.0")) {
        struct SampleEntry {
            const char *label;
            const char *path;
        };
        static constexpr SampleEntry samples[]{{"Hello World", "examples/Samat/v1.0/hello.st"},
                                               {"계산기", "examples/Samat/v1.0/calculator.st"},
                                               {"조건문과 반복문", "examples/Samat/v1.0/conditions-loops.st"},
                                               {"함수 호출", "examples/Samat/v1.0/functions.st"},
                                               {"訓C正音 함수 예제",
                                                "examples/Samat/v1.0/functions-korean.st"},
                                               {"Pong 게임", "examples/Samat/pong.st"}};
        for (const auto &sample : samples)
            if (ImGui::Selectable(sample.label)) {
                std::filesystem::path samplePath = utf8ToPath(sample.path);
                if (!std::filesystem::exists(samplePath)) {
                    if (const char *basePath = SDL_GetBasePath(); basePath != nullptr) {
                        const auto executableDirectory = utf8ToPath(basePath);
                        const auto packagedPath = executableDirectory / samplePath;
                        const auto developmentPath = executableDirectory.parent_path() / samplePath;
                        if (std::filesystem::exists(packagedPath))
                            samplePath = packagedPath;
                        else if (std::filesystem::exists(developmentPath))
                            samplePath = developmentPath;
                    }
                }
                languagePendingFilePath_ = pathToUtf8(samplePath);
                languagePendingDocumentAction_ = 3;
                if (languageDirty_)
                    ImGui::OpenPopup("저장하지 않은 변경사항");
                else
                    performDocumentAction();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("기본 코드")) {
        languageCodeBuffer_ = languageStarterCode;
        languageDirty_ = true;
        languageStatus_ = "기본 계산 코드를 불러왔어요.";
        languageOutput_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Factorial 예제")) {
        languageCodeBuffer_ = languageKoreanSyntax_
                                  ? "함수 factorial(n):\n    n이 1보다 작거나 같다면:\n        1을 "
                                    "반환한다.\n    n * factorial(n - 1)을 반환한다.\n숫자 변수 result를 "
                                    "factorial(5)로 정한다.\nresult를 출력한다.\n"
                                  : "fn factorial(n):\n    if n <= 1:\n        return 1\n    return n * "
                                    "factorial(n - 1)\n\nprint(factorial(5))\n";
        languageStatus_ = "재귀 함수 예제를 불러왔어요.";
        languageDirty_ = true;
        languageOutput_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("네이티브 Factorial")) {
        languageCodeBuffer_ =
            languageKoreanSyntax_
                ? "함수 factorial(n):\n    n이 1보다 작거나 같다면:\n        1을 반환한다.\n    n * "
                  "factorial(n - 1)을 반환한다.\n함수 main():\n    factorial(10)을 반환한다.\n"
                : "fn factorial(n):\n    if n <= 1:\n        return 1\n    return n * factorial(n - 1)\n\nfn "
                  "main():\n    return factorial(10)\n";
        languageStatus_ = "네이티브 x64 대상으로 사용할 factorial/main 예제예요.";
        languageDirty_ = true;
        languageOutput_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("FizzBuzz 예제")) {
        languageCodeBuffer_ =
            "let n = 1\nwhile n <= 100:\n    if n % 15 == 0:\n        print(\"FizzBuzz\")\n"
            "    else:\n        if n % 3 == 0:\n            print(\"Fizz\")\n        else:\n"
            "            if n % 5 == 0:\n                print(\"Buzz\")\n            else:\n                "
            "print(n)\n"
            "    n += 1\n";
        languageStatus_ = "FizzBuzz 예제를 불러왔어요.";
        languageDirty_ = true;
        languageOutput_.clear();
    }

    ImGui::SameLine();
    if (ImGui::Button("컴파일 검사")) {
        Program program;
        Diagnostic diagnostic;
        languageCompiled_ = parseEditorProgram(program, diagnostic);
        if (languageCompiled_) {
            std::vector<Diagnostic> errors;
            languageCompiled_ = check(program, errors);
            if (!errors.empty())
                diagnostic = errors.front();
        }
        languageErrorLine_ = languageCompiled_ ? 0 : diagnostic.line;
        languageDiagnostic_ = languageCompiled_ ? std::string{} : diagnostic.message;
        languageStatus_ = languageCompiled_ ? "컴파일 성공 · 공유 AST로 변환했어요."
                                            : languageBeginnerMode_ ? beginnerDiagnostic(diagnostic)
                                                                    : diagnostic.message;
        languageOutput_.clear();
    }
    ImGui::SameLine();
    const bool runActive = languageRun_.snapshot(false).running;
    ImGui::BeginDisabled(runActive);
    if (ImGui::Button("실행 · Interpreter")) {
        Program program;
        Diagnostic diagnostic;
        if (!parseEditorProgram(program, diagnostic)) {
            languageCompiled_ = false;
            languageErrorLine_ = diagnostic.line;
            languageDiagnostic_ = diagnostic.message;
            languageStatus_ = languageBeginnerMode_ ? beginnerDiagnostic(diagnostic) : diagnostic.message;
            languageOutput_.clear();
        } else {
            std::vector<Diagnostic> errors;
            if (!check(program, errors)) {
                languageCompiled_ = false;
                const auto &error = errors.front();
                languageErrorLine_ = error.line;
                languageDiagnostic_ = error.message;
                languageStatus_ = languageBeginnerMode_ ? beginnerDiagnostic(error) : error.message;
                languageOutput_.clear();
            } else {
                RunOptions options;
                options.instructionBudget = 1'000'000;
                options.outputByteLimit = 1 * 1024 * 1024;
                options.recursionLimit = 128;
                bool hasMain = false, hasActions = false;
                for (const auto &statement : program.statements) {
                    hasMain |= statement.kind == Statement::Kind::Function && statement.name == "main";
                    hasActions |= statement.kind != Statement::Kind::Function &&
                                  statement.kind != Statement::Kind::Variable &&
                                  statement.kind != Statement::Kind::Import &&
                                  statement.kind != Statement::Kind::Event &&
                                  statement.kind != Statement::Kind::Enum &&
                                  statement.kind != Statement::Kind::Struct;
                }
                if (hasMain && !hasActions)
                    options.entryFunction = "main";
                options.eventName = "start";
                if (languageRun_.start(std::move(program), std::move(options))) {
                    languageCompiled_ = true;
                    languageErrorLine_ = 0;
                    languageDiagnostic_.clear();
                    languageStatus_ = "실행 중 · 정지 버튼으로 언제든 멈출 수 있어요.";
                    languageOutput_.clear();
                } else {
                    languageStatus_ = "이전 실행이 아직 끝나지 않았어요.";
                }
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!runActive);
    if (ImGui::Button("정지")) {
        languageRun_.requestStop();
        languageStatus_ = "실행 중인 코드를 멈추고 있어요.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("현재 사용자 권한으로 실행됩니다. 신뢰할 수 있는 .st 파일만 실행하세요.");
    ImGui::SameLine();
    ImGui::BeginDisabled(runActive);
    if (ImGui::Button("x64 네이티브 main 실행")) {
        Program program;
        Diagnostic diagnostic;
        const bool parsed = parseEditorProgram(program, diagnostic);
        if (!parsed) {
            languageCompiled_ = false;
            languageStatus_ = diagnostic.message;
            languageOutput_.clear();
        } else {
            ir::Module module;
            ir::LoweringDiagnostic lowering;
            if (!ir::lower(program, module, lowering)) {
                languageCompiled_ = false;
                languageStatus_ = "네이티브 변환 제한: " + lowering.message;
                languageOutput_.clear();
            } else
                try {
                    ir::X64Backend backend;
                    const ir::NativeCode native = backend.compile(module);
                    const auto value = native.invoke("main");
                    languageCompiled_ = true;
                    languageStatus_ = "x86-64 네이티브 실행 완료 · " +
                                      std::to_string(native.machineCode("main").size()) + " bytes";
                    languageOutput_ = "결과: " + std::to_string(value) + "\n\nJM IR\n" + ir::format(module);
                } catch (const std::exception &error) {
                    languageCompiled_ = false;
                    languageStatus_ = error.what();
                    languageOutput_.clear();
                }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("JM IR 보기")) {
        Program program;
        Diagnostic diagnostic;
        ir::Module module;
        ir::LoweringDiagnostic lowering;
        if (!parseEditorProgram(program, diagnostic))
            languageStatus_ = diagnostic.message;
        else if (!ir::lower(program, module, lowering))
            languageStatus_ = lowering.message;
        else {
            languageOutput_ = ir::format(module);
            languageStatus_ = "JM IR 변환 완료";
        }
    }
    ImGui::BeginDisabled(!ir::LLVMBackend::available());
    if (ImGui::Button("LLVM JIT main 실행")) {
        Program program;
        Diagnostic diagnostic;
        ir::Module module;
        ir::LoweringDiagnostic lowering;
        if (!parseEditorProgram(program, diagnostic))
            languageStatus_ = diagnostic.message;
        else if (!ir::lower(program, module, lowering))
            languageStatus_ = lowering.message;
        else
            try {
                ir::LLVMBackend backend;
                auto code = backend.compile(module);
                languageOutput_ = "결과: " + code.invokeValue("main").toString();
                languageStatus_ = "LLVM JIT 실행 완료";
            } catch (const std::exception &error) {
                languageStatus_ = error.what();
            }
    }
    ImGui::SameLine();
    if (ImGui::Button("LLVM IR 보기")) {
        Program program;
        Diagnostic diagnostic;
        ir::Module module;
        ir::LoweringDiagnostic lowering;
        if (!parseEditorProgram(program, diagnostic))
            languageStatus_ = diagnostic.message;
        else if (!ir::lower(program, module, lowering))
            languageStatus_ = lowering.message;
        else
            try {
                languageOutput_ = ir::LLVMBackend{}.emitIR(module);
                languageStatus_ = "LLVM IR 검증 완료";
            } catch (const std::exception &error) {
                languageStatus_ = error.what();
            }
    }
    ImGui::SameLine();
    if (ImGui::Button("AOT 빌드")) {
        Program program;
        Diagnostic diagnostic;
        ir::Module module;
        ir::LoweringDiagnostic lowering;
        if (!parseEditorProgram(program, diagnostic))
            languageStatus_ = diagnostic.message;
        else if (!ir::lower(program, module, lowering))
            languageStatus_ = lowering.message;
        else
            try {
                std::filesystem::create_directories("build/editor");
#ifdef _WIN32
                const std::string output = "build/editor/JMProgram.exe";
#else
                const std::string output = "build/editor/JMProgram";
#endif
                ir::LLVMBackend{}.build(module, output);
                languageStatus_ = "AOT 실행 파일 생성: " + output;
            } catch (const std::exception &error) {
                languageStatus_ = error.what();
            }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!ir::LLVMBackend::available())
        ImGui::TextDisabled("LLVM 개발 패키지가 없어 LLVM JIT / IR / AOT가 비활성화돼 있어요.");

    const float editorHeight = std::max(180.0F, ImGui::GetContentRegionAvail().y - 160.0F);
    LanguageCompletionContext completion;
    const std::vector<std::string> codeKeywords{
        "let", "const", "fn", "if", "else", "while", "for", "in", "return", "break", "continue",
        "true", "false", "null", "and", "or", "not", "import", "on", "enum", "struct"};
    const std::vector<std::string> koreanKeywords{
        "함수", "변수", "상수", "라면", "아니라면", "동안", "반환한다", "반복하며", "반복을 멈춘다.",
        "다음 반복을 진행한다.", "가져온다", "실행한다", "출력한다", "참", "거짓", "시작할 때:"};
    const std::vector<std::string> builtinNames{
        "print", "println", "assert", "len", "length", "abs", "min", "max", "round", "floor", "ceil",
        "sqrt", "pow", "sin", "cos", "tan", "int", "float", "string", "bool", "bitXor", "vector2",
        "vector3", "color", "range", "append", "push", "pop", "clear", "Vector2", "Vector3", "Color"};
    for (const auto &word : codeKeywords)
        addCompletionSymbol(completion, word, "Samat 문법 키워드입니다.");
    if (languageKoreanSyntax_)
        for (const auto &word : koreanKeywords)
            addCompletionSymbol(completion, word, "訓C正音 문법 키워드입니다.");
    for (const auto &word : builtinNames)
        if (const auto info = standardFunction(word))
            addCompletionSymbol(completion, word, info->documentation + " · 모듈 " + info->module + " · " +
                                                               std::to_string(info->minimumArity) + "개 이상 인자");
        else
            addCompletionSymbol(completion, word, "Samat 내장 함수 또는 값입니다.");
    for (const auto &type : {"Int", "Float", "Bool", "String", "List", "Map", "Tuple", "Range", "Vector2",
                             "Vector3", "Color", "Entity", "Optional", "정수", "실수", "논리", "문자열",
                             "목록", "지도", "숫자"})
        addCompletionSymbol(completion, type, "Samat 기본 자료형입니다.");
    for (const auto &module : {"math", "console", "collections", "jm.random", "jm.time", "jm.io", "jm.math",
                               "jm.console", "jm.collections"})
        if (standardModule(module))
            addCompletionSymbol(completion, module, "가져올 수 있는 표준 모듈입니다.");
    Program symbols;
    Diagnostic symbolsDiagnostic;
    if (parseEditorProgram(symbols, symbolsDiagnostic))
        collectStatementCompletions(symbols.statements, completion);
    std::sort(completion.words.begin(), completion.words.end());
    completion.words.erase(std::unique(completion.words.begin(), completion.words.end()), completion.words.end());
    auto metadataRegistry = engineNativeFunctions();
    if (metadataRegistry.hasModule("jm.game"))
        addCompletionSymbol(completion, "jm.game", "현재 엔진 API가 제공하는 게임 모듈입니다.");
    auto metadataEntries = metadataRegistry.allMetadata();
    for (const auto &entry : metadataEntries) {
        addCompletionSymbol(completion, entry.displayName, ir::metadataSignature(entry) + "\n" +
                                                               entry.documentation);
        if (languageKoreanSyntax_)
            addCompletionSymbol(completion, entry.koreanName,
                                ir::metadataSignature(entry) + "\n" + entry.documentation);
    }
    std::sort(completion.words.begin(), completion.words.end());
    completion.words.erase(std::unique(completion.words.begin(), completion.words.end()), completion.words.end());
    if (!ImGui::BeginTable("SamatStudioSplit", 2,
                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Code Editor", ImGuiTableColumnFlags_WidthStretch, 0.62F);
    ImGui::TableSetupColumn("Run Output", ImGuiTableColumnFlags_WidthStretch, 0.38F);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("소스 편집");
    ImGui::TextDisabled("현재 문법: %s%s", languageKoreanSyntax_ ? "訓C正音" : "Samat",
                        languageDirty_ ? " · 수정됨" : "");
    ImGui::TextUnformatted("편집기");
    ImGui::SameLine();
    ImGui::TextDisabled("줄 번호 · 문법 강조 · 자동 들여쓰기/괄호 · Ctrl+Space 자동완성");
    if (languageEditorTextSnapshot_ != languageCodeBuffer_) {
        languageEditor_->SetText(languageCodeBuffer_);
        languageEditorTextSnapshot_ = languageCodeBuffer_;
    }
    TextEditor::ErrorMarkers errorMarkers;
    if (languageErrorLine_ > 0)
        errorMarkers.emplace(static_cast<int>(languageErrorLine_), languageDiagnostic_);
    languageEditor_->SetErrorMarkers(errorMarkers);
    languageEditor_->SetTabCallback([&completion, editor = languageEditor_.get()](bool shift) {
        if (shift)
            return false;
        const std::string source = editor->GetText();
        const int cursor = static_cast<int>(editorByteOffset(source, editor->GetCursorPosition()));
        int start = cursor;
        while (start > 0) {
            const unsigned char ch = static_cast<unsigned char>(source[static_cast<std::size_t>(start - 1)]);
            if (!std::isalnum(ch) && ch != '_' && ch != '.' && ch < 0x80)
                break;
            --start;
        }
        int end = cursor;
        while (end < static_cast<int>(source.size())) {
            const unsigned char ch = static_cast<unsigned char>(source[static_cast<std::size_t>(end)]);
            if (!std::isalnum(ch) && ch != '_' && ch != '.' && ch < 0x80)
                break;
            ++end;
        }
        const std::string prefix = source.substr(static_cast<std::size_t>(start),
                                                 static_cast<std::size_t>(cursor - start));
        std::vector<std::string_view> matches;
        for (const auto &word : completion.words)
            if (prefix.empty() || word.rfind(prefix, 0) == 0)
                matches.push_back(word);
        if (matches.empty())
            return false;
        std::string common(matches.front());
        for (std::size_t i = 1; i < matches.size(); ++i) {
            std::size_t length = 0;
            while (length < common.size() && length < matches[i].size() && common[length] == matches[i][length])
                ++length;
            common.resize(length);
        }
        if (common.size() <= prefix.size())
            return false;
        editor->SetSelection(editorPositionAt(source, static_cast<std::size_t>(start)),
                             editorPositionAt(source, static_cast<std::size_t>(end)));
        editor->InsertText(common);
        return true;
    });
    languageEditor_->Render("##jm-language-core", ImVec2(-1.0F, editorHeight), true);
    const bool editorChanged = languageEditor_->IsTextChanged();
    if (editorChanged) {
        languageCodeBuffer_ = languageEditor_->GetText();
        languageEditorTextSnapshot_ = languageCodeBuffer_;
        languageDirty_ = true;
        Program parsed;
        Diagnostic diagnostic;
        if (parseEditorProgram(parsed, diagnostic)) {
            languageErrorLine_ = 0;
            languageDiagnostic_.clear();
        } else {
            languageErrorLine_ = diagnostic.line;
            languageDiagnostic_ = diagnostic.message;
            languageStatus_ = languageBeginnerMode_ ? beginnerDiagnostic(diagnostic) : diagnostic.message;
        }
    }
    const bool editorFocused = languageEditor_->IsFocused();
    if (editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
#ifdef _WIN32
        if (languageFilePath_.empty()) {
            if (const auto selected = studio::chooseSaveScriptFile()) {
                languagePathInput_ = pathToUtf8(*selected);
                saveDocument(true);
            }
        } else
#endif
            saveDocument();
    }
    if (editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
        languagePendingDocumentAction_ = 1;
        if (languageDirty_)
            ImGui::OpenPopup("저장하지 않은 변경사항");
        else
            performDocumentAction();
    }
    if (editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
#ifdef _WIN32
        if (const auto selected = studio::chooseOpenScriptFile()) {
            languagePendingDocumentAction_ = 2;
            languagePendingFilePath_ = pathToUtf8(*selected);
            if (languageDirty_)
                ImGui::OpenPopup("저장하지 않은 변경사항");
            else
                performDocumentAction();
        }
#else
        languageStatus_ = "파일 경로를 입력한 뒤 열기 버튼을 사용해 주세요.";
#endif
    }

    const auto cursorPosition = languageEditor_->GetCursorPosition();
    completion.cursor = static_cast<int>(editorByteOffset(languageCodeBuffer_, cursorPosition));
    completion.wordStart = completion.cursor;
    while (completion.wordStart > 0) {
        const unsigned char ch = static_cast<unsigned char>(languageCodeBuffer_[completion.wordStart - 1]);
        if (!std::isalnum(ch) && ch != '_' && ch != '.' && ch < 0x80)
            break;
        --completion.wordStart;
    }
    completion.wordEnd = completion.cursor;
    while (completion.wordEnd < static_cast<int>(languageCodeBuffer_.size())) {
        const unsigned char ch = static_cast<unsigned char>(languageCodeBuffer_[completion.wordEnd]);
        if (!std::isalnum(ch) && ch != '_' && ch != '.' && ch < 0x80)
            break;
        ++completion.wordEnd;
    }
    completion.prefix = languageCodeBuffer_.substr(completion.wordStart,
                                                    completion.cursor - completion.wordStart);
    completion.matches.clear();
    for (const auto &word : completion.words)
        if (completion.prefix.empty() || word.rfind(completion.prefix, 0) == 0)
            completion.matches.push_back(word);
    if (editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space))
        ImGui::OpenPopup("Samat 자동완성");
    if (!completion.matches.empty() && ImGui::BeginPopup("Samat 자동완성")) {
        ImGui::TextUnformatted("현재 파일 이름 · 함수 매개변수 · 엔진 API");
        const std::size_t limit = std::min<std::size_t>(completion.matches.size(), 40);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &suggestion = completion.matches[index];
            ImGui::PushID(static_cast<int>(index));
            const bool selected = ImGui::Selectable(suggestion.c_str(), index == 0);
            if (ImGui::IsItemHovered())
                if (auto detail = completion.descriptions.find(suggestion);
                    detail != completion.descriptions.end() && !detail->second.empty())
                    ImGui::SetTooltip("%s", detail->second.c_str());
            if (selected) {
                const auto start = static_cast<std::size_t>(completion.wordStart);
                const auto end = static_cast<std::size_t>(completion.wordEnd);
                languageEditor_->SetSelection(editorPositionAt(languageCodeBuffer_, start),
                                              editorPositionAt(languageCodeBuffer_, end));
                languageEditor_->InsertText(suggestion);
                languageCodeBuffer_ = languageEditor_->GetText();
                languageEditorTextSnapshot_ = languageCodeBuffer_;
                languageDirty_ = true;
                ImGui::CloseCurrentPopup();
            }
            if (index == 0)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    if (auto keyRange = studio::inputKeyStringAt(languageCodeBuffer_,
                                                 static_cast<std::size_t>(completion.cursor));
        keyRange && ImGui::SmallButton("키 선택")) {
        languageKeyRange_ = *keyRange;
        languageKeyRangeValid_ = true;
        ImGui::OpenPopup("입력 키 선택");
    }
    if (ImGui::BeginPopup("입력 키 선택")) {
        ImGui::InputText("검색", &languageKeySearch_);
        struct KeyChoice {
            std::string label;
            std::string name;
            std::string description;
        };
        static const std::vector<KeyChoice> keys = [] {
            std::vector<KeyChoice> values{{"Space", "space", "스페이스 키"},
                                          {"Enter", "return", "엔터 키"},
                                          {"Escape", "escape", "취소 키"},
                                          {"Tab", "tab", "탭 키"},
                                          {"Backspace", "backspace", "지우기 키"},
                                          {"Delete", "delete", "삭제 키"},
                                          {"Left Arrow", "left", "왼쪽 방향키"},
                                          {"Right Arrow", "right", "오른쪽 방향키"},
                                          {"Up Arrow", "up", "위쪽 방향키"},
                                          {"Down Arrow", "down", "아래쪽 방향키"},
                                          {"Left Ctrl", "left_ctrl", "왼쪽 Ctrl 키"},
                                          {"Right Ctrl", "right_ctrl", "오른쪽 Ctrl 키"},
                                          {"Left Shift", "left_shift", "왼쪽 Shift 키"},
                                          {"Right Shift", "right_shift", "오른쪽 Shift 키"}};
            for (char value = 'a'; value <= 'z'; ++value)
                values.push_back({std::string(1, static_cast<char>(std::toupper(
                                      static_cast<unsigned char>(value)))),
                                  std::string(1, value), "문자 키"});
            for (char value = '0'; value <= '9'; ++value)
                values.push_back({std::string(1, value), std::string(1, value), "숫자 키"});
            return values;
        }();
        for (const auto &key : keys) {
            if (!languageKeySearch_.empty() &&
                key.label.find(languageKeySearch_) == std::string::npos &&
                key.name.find(languageKeySearch_) == std::string::npos)
                continue;
            if (ImGui::Selectable(key.label.c_str())) {
                if (languageKeyRangeValid_ && languageKeyRange_.end <= languageCodeBuffer_.size()) {
                    languageEditor_->SetSelection(editorPositionAt(languageCodeBuffer_, languageKeyRange_.begin),
                                                  editorPositionAt(languageCodeBuffer_, languageKeyRange_.end));
                    languageEditor_->InsertText(key.name);
                    languageCodeBuffer_ = languageEditor_->GetText();
                    languageEditorTextSnapshot_ = languageCodeBuffer_;
                    languageDirty_ = true;
                }
                languageKeyRangeValid_ = false;
                languageKeySearch_.clear();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s · input.isHeld(\"%s\") 및 input.wasPressed(\"%s\")에서 사용",
                                  key.description.c_str(), key.name.c_str(), key.name.c_str());
        }
        ImGui::EndPopup();
    }
    if (languageErrorLine_ > 0) {
        ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.30F, 1.0F), "문법 오류 · 줄 %zu", languageErrorLine_);
        if (!languageDiagnostic_.empty())
            ImGui::TextWrapped("%s", languageBeginnerMode_ ? beginnerDiagnostic(Diagnostic{
                                                                  languageDiagnostic_, languageErrorLine_})
                                                           .c_str()
                                                         : languageDiagnostic_.c_str());
    }
    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginTabBar("SamatStudioOutput")) {
            if (ImGui::BeginTabItem("콘솔")) {
                ImGui::BeginChild("SamatStudioConsole", ImVec2(-1.0F, editorHeight),
                                  ImGuiChildFlags_Borders);
                ImGui::TextUnformatted(languageOutput_.empty() ? "실행 결과가 여기에 표시됩니다."
                                                               : languageOutput_.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("진단")) {
                if (languageStatus_.empty())
                    ImGui::TextUnformatted("문법 검사나 실행을 시작해 보세요.");
                else
                    ImGui::TextWrapped("%s", languageStatus_.c_str());
                if (languageErrorLine_ > 0) {
                    ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.30F, 1.0F), "문제 위치 · 줄 %zu",
                                       languageErrorLine_);
                    std::size_t lineStart = 0;
                    for (std::size_t line = 1; line < languageErrorLine_ && lineStart < languageCodeBuffer_.size();
                         ++line) {
                        const auto newline = languageCodeBuffer_.find('\n', lineStart);
                        if (newline == std::string::npos) {
                            lineStart = languageCodeBuffer_.size();
                            break;
                        }
                        lineStart = newline + 1;
                    }
                    if (lineStart < languageCodeBuffer_.size()) {
                        auto lineEnd = languageCodeBuffer_.find('\n', lineStart);
                        if (lineEnd == std::string::npos)
                            lineEnd = languageCodeBuffer_.size();
                        const std::string excerpt = languageCodeBuffer_.substr(lineStart, lineEnd - lineStart);
                        ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.40F, 1.0F), "> %s", excerpt.c_str());
                    }
                    if (!languageDiagnostic_.empty() && !languageBeginnerMode_)
                        ImGui::TextWrapped("%s", languageDiagnostic_.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("게임 미리보기")) {
                ImGui::TextWrapped("게임 장면 스크립트는 상단의 코드 작업공간에서 게임 미리보기와 함께 실행할 수 있습니다. 독립 Samat 코드는 장면 권한 없이 실행됩니다.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
    }
    ImGui::EndTable();
    if (editorFocused && ImGui::IsKeyPressed(ImGuiKey_F1))
        ImGui::OpenPopup("Samat 자동완성");
    if (ImGui::CollapsingHeader("값 편집")) {
        Program values;
        Diagnostic diagnostic;
        if (parseEditorProgram(values, diagnostic))
            for (auto &item : values.statements) {
                if (item.kind != Statement::Kind::Variable || !item.expression ||
                    item.expression->kind != Expression::Kind::Literal)
                    continue;
                auto &value = item.expression->literal;
                bool changed = false;
                ImGui::PushID(item.name.c_str());
                ImGui::TextUnformatted(item.name.c_str());
                ImGui::SameLine();
                if (item.declaredType == Type::Optional &&
                    (item.elementType == Type::Int || item.elementType == Type::Float ||
                     item.elementType == Type::Bool || item.elementType == Type::String)) {
                    bool hasValue = !value.isNull();
                    if (ImGui::Checkbox("값 있음", &hasValue)) {
                        changed = true;
                        if (!hasValue)
                            value = Value{};
                        else
                            value = item.elementType == Type::Int     ? Value(int64_t{0})
                                    : item.elementType == Type::Float ? Value(0.0)
                                    : item.elementType == Type::Bool  ? Value(false)
                                                                      : Value("");
                    }
                    ImGui::SameLine();
                }
                if (auto number = std::get_if<double>(&value.data))
                    changed |= ImGui::DragScalar("##value", ImGuiDataType_Double, number, 0.5F);
                else if (auto integer = std::get_if<int64_t>(&value.data))
                    changed |= ImGui::InputScalar("##value", ImGuiDataType_S64, integer);
                else if (auto boolean = std::get_if<bool>(&value.data))
                    changed |= ImGui::Checkbox("##value", boolean);
                else if (auto text = std::get_if<std::string>(&value.data))
                    changed |= ImGui::InputText("##value", text);
                else
                    ImGui::TextUnformatted("빈 값");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s · 선언 줄 %zu",
                                annotationName(item.declaredType, item.elementType).c_str(), item.line);
                    if (languageRuntime_) {
                        auto snapshot = languageRuntime_->inspect();
                        if (snapshot.contains(item.name))
                            ImGui::Text("현재 값: %s", snapshot.at(item.name).c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                if (changed) {
                    languageCodeBuffer_ = languageKoreanSyntax_ ? renderKorean(values) : renderCode(values);
                    if (languageRuntime_)
                        applyLanguageEdit(languageCodeBuffer_, languageKoreanSyntax_);
                    break;
                }
            }
        ImGui::TextUnformatted(languageRuntime_ ? "실행 중 · 호환 변경은 즉시 적용됩니다."
                                                : "편집한 값은 소스에 저장됩니다.");
    }
    if (ImGui::CollapsingHeader("기능 찾기")) {
        static std::string search;
        ImGui::InputText("검색", &search);
        bool advanced = ImGui::CollapsingHeader("고급 기능");
        for (const auto &entry : metadataEntries) {
            if ((entry.tooling.advanced && !advanced) || !ir::metadataMatches(entry, search))
                continue;
            auto label = entry.tooling.category + " · " + entry.tooling.beginnerName + "##" + entry.symbol;
            auto insertion = ir::metadataTemplate(entry, languageKoreanSyntax_);
            ImGui::BeginDisabled(insertion.empty());
            if (ImGui::SmallButton(label.c_str())) {
                auto text = insertion;
                if (!text.empty()) {
                    if (!languageCodeBuffer_.empty() && languageCodeBuffer_.back() != '\n')
                        languageCodeBuffer_ += '\n';
                    languageCodeBuffer_ += text;
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(entry.tooling.beginnerName.c_str());
                ImGui::TextWrapped("%s", entry.documentation.c_str());
                for (const auto &parameter : entry.tooling.parameters)
                    if (parameter.numeric)
                        ImGui::Text("%s · 추천 %.1f ~ %.1f", parameter.label.c_str(),
                                    parameter.recommendedMinimum, parameter.recommendedMaximum);
                if (ImGui::GetIO().KeyAlt) {
                    ImGui::TextUnformatted(ir::metadataSignature(entry).c_str());
                    ImGui::TextUnformatted(entry.symbol.c_str());
                } else
                    ImGui::TextUnformatted("Alt: 자세한 타입/심볼 정보");
                ImGui::EndTooltip();
            }
        }
        if (ImGui::TreeNode("값으로 기능 만들기")) {
            static std::map<std::string, double> parameterValues;
            for (auto entry : metadataEntries) {
                if (!ir::metadataMatches(entry, search))
                    continue;
                bool hasEditor = std::any_of(entry.tooling.parameters.begin(), entry.tooling.parameters.end(),
                                             [](const auto &parameter) { return parameter.numeric; });
                if (!hasEditor || entry.tooling.advanced)
                    continue;
                if (ImGui::TreeNode(entry.symbol.c_str(), "%s", entry.tooling.beginnerName.c_str())) {
                    for (size_t i = 0; i < entry.tooling.parameters.size(); ++i) {
                        auto &parameter = entry.tooling.parameters[i];
                        if (!parameter.numeric)
                            continue;
                        auto key = entry.symbol + "." + std::to_string(i);
                        if (!parameterValues.contains(key))
                            parameterValues[key] = parameter.initial;
                        auto &number = parameterValues[key];
                        float editable = static_cast<float>(number);
                        ImGui::PushID(key.c_str());
                        if (ImGui::SliderFloat(parameter.label.c_str(), &editable,
                                               static_cast<float>(parameter.minimum),
                                               static_cast<float>(parameter.maximum)))
                            number = editable;
                        for (const auto &[name, value] : parameter.presets) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton(name.c_str()))
                                number = value;
                        }
                        parameter.initial = number;
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("이 값으로 소스 삽입"))
                        languageCodeBuffer_ += "\n" + ir::metadataTemplate(entry, languageKoreanSyntax_);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        ImGui::TextUnformatted("같은 언어의 텍스트 템플릿을 삽입합니다.");
    }
    ImGui::BeginDisabled(playing_);
    ImGui::Checkbox("이 언어 스크립트로 Play 실행", &languagePlayEnabled_);
    ImGui::SameLine();
    ImGui::RadioButton("Interpreter", &languagePlayBackend_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ir::LLVMBackend::available());
    ImGui::RadioButton("LLVM JIT", &languagePlayBackend_, 1);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (languageRuntime_) {
        ImGui::TextUnformatted("실행 중 · 편집 가능");
        ImGui::SameLine();
        if (ImGui::Button("실행에 변경사항 적용"))
            applyLanguageEdit(languageCodeBuffer_, languageKoreanSyntax_);
        if (ImGui::CollapsingHeader("실행 값"))
            for (const auto &[name, value] : languageRuntime_->inspect())
                ImGui::Text("%s = %s", name.c_str(), value.c_str());
    }
    if (ImGui::CollapsingHeader("현재까지 사용할 수 있는 코드", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText(
            "언어 코어: 변수 변경, if/else, while/for, 함수·매개변수·반환·재귀, 산술·비교·논리식");
        ImGui::BulletText(
            "자료형/자료구조: Number, Boolean, String, List, Map · 인덱스 읽기/쓰기 · append/pop/length");
        ImGui::BulletText("내장 함수: print, assert, len, abs/min/max, round/floor/ceil, sqrt/pow, "
                          "sin/cos/tan, vector2/color");
        ImGui::BulletText("게임 스크립트: 시작 이벤트, 좌우 이동, 점프, 이동 속도 변수와 매개변수 함수");
        ImGui::BulletText("에디터 도움: 문법 컴파일 검사, 실행 결과, 키워드/함수 자동완성");
    }
}

void Application::captureViewport() {
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0F, available.x);
    available.y = std::max(1.0F, available.y);
    viewportX_ = topLeft.x;
    viewportY_ = topLeft.y;
    viewportWidth_ = available.x;
    viewportHeight_ = available.y;
    viewportVisible_ = true;
    viewportHovered_ =
        ImGui::IsMouseHoveringRect(topLeft, ImVec2(topLeft.x + available.x, topLeft.y + available.y));
    ImGui::Dummy(available);
}

void Application::saveProject() {
    try {
        if (!projectPathInput_.empty())
            projectRoot_ = utf8ToPath(projectPathInput_);
        projectName_ = projectName_.empty() ? "My First Game" : projectName_;
        if (codeMode_) {
            ScriptDiagnostic diagnostic;
            ScriptDocument parsed = script_;
            if (!parseScriptCode(codeBuffer_, scene_, parsed, diagnostic)) {
                scriptStatus_ = diagnosticText(diagnostic);
                projectStatus_ =
                    "Save cancelled because the code has errors. Correct it or apply a valid version first.";
                return;
            }
            script_ = std::move(parsed);
        }
        if (koreanEditMode_) {
            ScriptDiagnostic diagnostic;
            ScriptDocument parsed = script_;
            if (!parseScriptKorean(koreanBuffer_, scene_, parsed, diagnostic)) {
                scriptStatus_ = diagnosticText(diagnostic);
                projectStatus_ = "Save cancelled because the Korean script has errors.";
                return;
            }
            script_ = std::move(parsed);
        }
        ProjectSettings settings;
        settings.projectId = projectId_;
        settings.name = projectName_;
        ProjectStore::save(projectRoot_, settings, scene_, script_);
        projectStatus_ = "Saved to " + pathToUtf8(projectRoot_);
    } catch (const std::exception &exception) {
        projectStatus_ = std::string{"Save failed: "} + exception.what();
    }
}

void Application::openProject() {
    if (playing_)
        stopPlay();
    languageRuntime_.reset();
    try {
        const std::filesystem::path requestedRoot = utf8ToPath(projectPathInput_);
        ProjectDocument document = ProjectStore::load(requestedRoot);
        scene_ = std::move(document.scene);
        script_ = std::move(document.script);
        codeMode_ = false;
        koreanEditMode_ = false;
        codeBuffer_.clear();
        projectRoot_ = requestedRoot;
        projectPathInput_ = pathToUtf8(projectRoot_);
        projectName_ = document.settings.name;
        projectId_ = document.settings.projectId;
        playing_ = false;
        scene_.selectFirst(twoDimensional_ ? ObjectKind::Sprite2D : ObjectKind::Cube3D);
        projectStatus_ = "Opened " + projectName_;
    } catch (const std::exception &exception) {
        projectStatus_ = std::string{"Open failed: "} + exception.what();
    }
}

void Application::createProject() {
    if (playing_)
        stopPlay();
    languageRuntime_.reset();
    try {
        const std::filesystem::path requestedRoot = utf8ToPath(projectPathInput_);
        if (std::filesystem::exists(requestedRoot / "project.jm")) {
            throw std::runtime_error("That folder already has a project. Open it or choose a new folder.");
        }
        ProjectSettings settings = ProjectStore::makeNewProject(projectName_);
        Scene newScene;
        ScriptDocument newScript = makeDefaultScript(newScene);
        ProjectStore::save(requestedRoot, settings, newScene, newScript);
        scene_ = std::move(newScene);
        script_ = std::move(newScript);
        codeMode_ = false;
        koreanEditMode_ = false;
        codeBuffer_.clear();
        projectRoot_ = requestedRoot;
        projectName_ = settings.name;
        projectId_ = settings.projectId;
        playing_ = false;
        scene_.selectFirst(twoDimensional_ ? ObjectKind::Sprite2D : ObjectKind::Cube3D);
        projectStatus_ = "Created and saved " + projectName_;
    } catch (const std::exception &exception) {
        projectStatus_ = std::string{"New project failed: "} + exception.what();
    }
}

void Application::startLanguagePlay(std::string source, bool korean, EngineScriptBackend backend) {
    languagePlayBackend_ = backend == EngineScriptBackend::LLVM ? 1 : 0;
    if (playing_)
        throw std::runtime_error("Stop Play before changing the language script.");
    languageCodeBuffer_ = std::move(source);
    languageKoreanSyntax_ = korean;
    languagePlayEnabled_ = true;
    twoDimensional_ = true;
    togglePlaying();
    if (!playing_)
        throw std::runtime_error(languageStatus_);
}
void Application::loadHaerye(std::string source) {
    if (playing_)
        throw std::runtime_error("Stop Play before replacing the Scene with Haerye.");
    HaeryeDocument document;
    HaeryeDiagnostic diagnostic;
    if (!parseHaerye(source, document, diagnostic))
        throw std::runtime_error("Haerye " + diagnostic.toString());
    if (!validateHaerye(document, diagnostic))
        throw std::runtime_error("Haerye " + diagnostic.toString());
    Scene candidate = scene_;
    if (!instantiateHaerye(document, candidate, diagnostic))
        throw std::runtime_error("Haerye " + diagnostic.toString());
    scene_ = std::move(candidate);
    scene_.selectFirst(ObjectKind::Sprite2D);
    script_ = makeDefaultScript(scene_);
    twoDimensional_ = true;
    scriptStatus_ = "Loaded Haerye scene: " + scene_.name();
}
bool Application::applyLanguageEdit(std::string source, bool korean) {
    if (!languageRuntime_) {
        languageStatus_ = "Samat 게임 실행을 먼저 시작하세요.";
        return false;
    }
    script::Program candidate;
    script::Diagnostic diagnostic;
    bool parsed = korean ? script::parseKorean(source, candidate, diagnostic)
                         : script::parseCode(source, candidate, diagnostic);
    if (!parsed || !languageRuntime_->hotSwap(std::move(candidate), diagnostic)) {
        languageStatus_ = "변경사항을 적용하지 못했어요. 이전 코드로 실행을 계속합니다. " + diagnostic.code +
                          ": " + diagnostic.message;
        return false;
    }
    languageCodeBuffer_ = std::move(source);
    languageKoreanSyntax_ = korean;
    languageStatus_ = "변경사항 적용 완료 · 세대 " + std::to_string(languageRuntime_->generation());
    return true;
}
void Application::stopPlay() {
    if (playing_)
        togglePlaying();
}
void Application::togglePlaying() {
    if (playing_) {
        playing_ = false;
        languageRuntime_.reset();
        if (playSnapshot_)
            scene_ = std::move(*playSnapshot_);
        scene_.invalidateReferences();
        playSnapshot_.reset();
        requestedWorkspace_ = 0;
        scriptStatus_ = "실행을 멈췄어요.";
        return;
    }

    if (!twoDimensional_) {
        scriptStatus_ = "3D 장면 편집은 가능하지만, 현재 Samat 게임 실행은 2D 장면에서만 지원해요. 2D "
                        "장면으로 바꿔 주세요.";
        return;
    }

    if (languagePlayEnabled_) {
        script::Program program;
        script::Diagnostic diagnostic;
        auto parsed = languageKoreanSyntax_ ? script::parseKorean(languageCodeBuffer_, program, diagnostic)
                                            : script::parseCode(languageCodeBuffer_, program, diagnostic);
        if (!parsed) {
            languageStatus_ = diagnostic.message;
            return;
        }
        std::string playerId;
        for (const auto &object : scene_.objects())
            if (object.name == "Player") {
                playerId = object.id;
                break;
            }
        playSnapshot_ = scene_;
        try {
            languageRuntime_ = std::make_unique<EngineEventRuntime>(
                scene_, playerId, std::move(program),
                languagePlayBackend_ == 1 ? EngineScriptBackend::LLVM : EngineScriptBackend::Interpreter);
            languageRuntime_->start();
        } catch (const std::exception &error) {
            languageRuntime_.reset();
            scene_ = std::move(*playSnapshot_);
            playSnapshot_.reset();
            languageStatus_ = error.what();
            return;
        }
        scriptAccumulator_ = 0;
        playing_ = true;
        activeWorkspace_ = 2;
        requestedWorkspace_ = 2;
        tutorialPlayRun_ = true;
        scriptStatus_ = "Samat 게임 실행을 시작했어요.";
        return;
    }
    ScriptDocument candidate = script_;
    if (codeMode_) {
        ScriptDiagnostic parseDiagnostic;
        if (!parseScriptCode(codeBuffer_, scene_, candidate, parseDiagnostic)) {
            scriptStatus_ = diagnosticText(parseDiagnostic);
            return;
        }
    }
    if (koreanEditMode_) {
        ScriptDiagnostic parseDiagnostic;
        if (!parseScriptKorean(koreanBuffer_, scene_, candidate, parseDiagnostic)) {
            scriptStatus_ = diagnosticText(parseDiagnostic);
            return;
        }
    }

    ScriptDiagnostic diagnostic;
    ScriptProgram program;
    if (!compileScript(candidate, scene_, program, diagnostic)) {
        scriptStatus_ = diagnostic.message;
        return;
    }
    script_ = std::move(candidate);
    compiledScript_ = std::move(program);
    playSnapshot_ = scene_;
    executeScriptStart(compiledScript_, scene_);
    scriptAccumulator_ = 0.0F;
    playing_ = true;
    activeWorkspace_ = 2;
    requestedWorkspace_ = 2;
    tutorialPlayRun_ = true;
    scriptStatus_ = "스크립트를 확인하고 실행을 시작했어요.";
}

} // namespace jm
