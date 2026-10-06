#include "JMEngine/Core/Application.hpp"

#include "JMEngine/Project/ProjectStore.hpp"
#include "JMEngine/Renderer/GLApi.hpp"
#include "JMEngine/Renderer/Renderer.hpp"
#include "JMEngine/Renderer/Viewport.hpp"
#include "JMEngine/Script/JMIR.hpp"
#include "JMEngine/Script/LanguageCore.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
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

struct LanguageCompletionContext {
    std::vector<std::string> words;
    std::vector<std::string> matches;
    std::string prefix;
    int wordStart{0};
    int cursor{0};
};

int languageCompletionCallback(ImGuiInputTextCallbackData *data) {
    auto *context = static_cast<LanguageCompletionContext *>(data->UserData);
    if (context == nullptr)
        return 0;
    int start = data->CursorPos;
    while (start > 0) {
        const unsigned char ch = static_cast<unsigned char>(data->Buf[start - 1]);
        if (!std::isalnum(ch) && ch != '_' && ch != '.' && ch < 128)
            break;
        --start;
    }
    context->wordStart = start;
    context->cursor = data->CursorPos;
    const std::string prefix(data->Buf + start, static_cast<std::size_t>(data->CursorPos - start));
    context->prefix = prefix;
    context->matches.clear();
    for (const std::string &word : context->words) {
        if (prefix.empty() || word.rfind(prefix, 0) == 0)
            context->matches.push_back(word);
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion && !context->matches.empty()) {
        std::string completion = context->matches.front();
        for (std::size_t index = 1; index < context->matches.size(); ++index) {
            std::size_t common = 0;
            while (common < completion.size() && common < context->matches[index].size() &&
                   completion[common] == context->matches[index][common])
                ++common;
            completion.resize(common);
        }
        if (completion.size() > prefix.size()) {
            data->DeleteChars(start, data->CursorPos - start);
            data->InsertChars(start, completion.c_str());
        }
    }
    return 0;
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
    script_ = makeDefaultScript(scene_);
    languageCodeBuffer_ = languageStarterCode;
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
        ImGui::GetStyle().WindowRounding = 6.0F;
        ImGui::GetStyle().FrameRounding = 4.0F;
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
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running_ = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat)
                break;
            if (event.key.key == SDLK_F5 && (event.key.mod & SDL_KMOD_SHIFT) == 0)
                togglePlaying();
            if (event.key.key == SDLK_F5 && (event.key.mod & SDL_KMOD_SHIFT) != 0 && playing_)
                togglePlaying();
            if (event.key.key == SDLK_SPACE && playing_ && !io.WantTextInput)
                spacePressedThisFrame_ = true;
            if (playing_ && !io.WantTextInput)
                keysPressedThisFrame_.insert(scriptKeyName(event.key.scancode));
            if (io.WantCaptureKeyboard)
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
    SDL_SetWindowTitle(window_, enabled ? "JOSAMOSA ENGINE | 2D Scene" : "JOSAMOSA ENGINE | 3D Scene");
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
                             cameraPanX2D_, cameraPanY2D_, zoom2D_, scene_.objects());
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

    const float toolbarWidth = std::min(920.0F, std::max(700.0F, io.DisplaySize.x - 20.0F));
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - toolbarWidth) * 0.5F, 8.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbarWidth, 72.0F), ImGuiCond_Always);
    if (ImGui::Begin("JM Engine Toolbar", nullptr, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::Button(playing_ ? "■ 정지" : "▶ 실행", ImVec2(85.0F, 30.0F)))
            togglePlaying();
        ImGui::SameLine();
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("3D 장면", ImVec2(80.0F, 30.0F)))
            set2DMode(false);
        ImGui::SameLine();
        if (ImGui::Button("2D 장면", ImVec2(80.0F, 30.0F)))
            set2DMode(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(playing_ ? "←/→ 이동 · Space 점프" : "F5 실행 · Shift+F5 정지");
        ImGui::SameLine();
        if (ImGui::Button("Save"))
            saveProject();
        ImGui::SameLine();
        if (ImGui::Button("Project..."))
            projectDialogOpen_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", projectName_.c_str());
    }
    ImGui::End();

    if (projectDialogOpen_) {
        ImGui::OpenPopup("Project");
        projectDialogOpen_ = false;
    }
    if (ImGui::BeginPopupModal("Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Project name", &projectName_);
        ImGui::InputText("Project folder", &projectPathInput_);
        ImGui::TextWrapped("Projects save project.jm and scenes/main.scene in this folder.");
        if (!projectStatus_.empty())
            ImGui::TextWrapped("%s", projectStatus_.c_str());
        if (ImGui::Button("New Project", ImVec2(120.0F, 0.0F)))
            createProject();
        ImGui::SameLine();
        if (ImGui::Button("Save Project", ImVec2(120.0F, 0.0F)))
            saveProject();
        ImGui::SameLine();
        if (ImGui::Button("Open Project", ImVec2(120.0F, 0.0F)))
            openProject();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(80.0F, 0.0F)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const bool focusedWorkspace = activeWorkspace_ != 0;
    const float workspaceX = focusedWorkspace ? 10.0F : 250.0F;
    const float workspaceWidth = std::max(300.0F, io.DisplaySize.x - (focusedWorkspace ? 20.0F : 550.0F));
    ImGui::SetNextWindowPos(ImVec2(workspaceX, 94.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workspaceWidth, std::max(250.0F, io.DisplaySize.y - 108.0F)),
                             ImGuiCond_Always);
    if (ImGui::Begin("작업 공간", nullptr,
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::BeginTabBar("JMWorkspaceTabs")) {
            if (ImGui::BeginTabItem("장면", nullptr,
                                    requestedWorkspace_ == 0 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 0;
                ImGui::TextDisabled(twoDimensional_
                                        ? "2D 장면 · 가운데 버튼 드래그: 화면 이동 · 휠: 확대/축소"
                                        : "3D 장면 · 왼쪽 버튼 드래그: 궤도 회전 · 휠: 확대/축소");
                ImGui::SameLine();
                if (twoDimensional_) {
                    if (ImGui::SmallButton("+ 2D 오브젝트"))
                        scene_.create(ObjectKind::Sprite2D);
                } else if (ImGui::SmallButton("+ 3D 큐브")) {
                    scene_.create(ObjectKind::Cube3D);
                }
                captureViewport();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("코드", nullptr,
                                    requestedWorkspace_ == 1 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 1;
                drawCodePanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("실행 화면", nullptr,
                                    requestedWorkspace_ == 2 ? ImGuiTabItemFlags_SetSelected
                                                             : ImGuiTabItemFlags_None)) {
                activeWorkspace_ = 2;
                ImGui::TextDisabled(playing_ ? "게임 실행 중 · ←/→ 이동 · Space 점프"
                                             : "게임 미리보기 · 실행을 누르면 시작");
                captureViewport();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            requestedWorkspace_ = -1;
        }
    }
    ImGui::End();

    if (activeWorkspace_ == 0) {
        ImGui::SetNextWindowPos(ImVec2(10.0F, 94.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(230.0F, std::max(250.0F, io.DisplaySize.y - 108.0F)),
                                 ImGuiCond_Always);
        if (ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled(twoDimensional_ ? "2D 오브젝트" : "3D 오브젝트");
            if (twoDimensional_) {
                if (ImGui::Button("+ 2D 오브젝트", ImVec2(-1.0F, 28.0F)))
                    scene_.create(ObjectKind::Sprite2D);
            } else if (ImGui::Button("+ 3D 큐브", ImVec2(-1.0F, 28.0F))) {
                scene_.create(ObjectKind::Cube3D);
            }
            ImGui::Separator();
            if (ImGui::BeginChild("Scene objects", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders)) {
                for (const GameObject &object : scene_.objects()) {
                    if (object.kind != activeKind)
                        continue;
                    const GameObject *selected = scene_.selected();
                    ImGui::PushID(object.id.c_str());
                    const char *layerNames[] = {"배경", "월드", "캐릭터", "효과", "UI"};
                    const std::string rowName =
                        object.name + "  ·  " + layerNames[std::clamp(object.layer, 0, 4)];
                    if (ImGui::Selectable(rowName.c_str(),
                                          selected != nullptr && selected->id == object.id)) {
                        scene_.select(object.id);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 290.0F, 94.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(280.0F, std::max(250.0F, io.DisplaySize.y - 108.0F)),
                                 ImGuiCond_Always);
        if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            GameObject *object = scene_.selected();
            if (object == nullptr || object->kind != activeKind) {
                ImGui::TextDisabled("Select an object from the hierarchy.");
            } else {
                ImGui::TextDisabled(object->kind == ObjectKind::Cube3D ? "3D CUBE" : "2D SPRITE");
                ImGui::InputText("Name", &object->name);
                ImGui::SeparatorText("Transform");
                if (object->kind == ObjectKind::Cube3D) {
                    ImGui::DragFloat3("Position XYZ", &object->position.x, 0.05F, -100.0F, 100.0F, "%.2f");
                    ImGui::DragFloat3("Rotation XYZ", &object->rotationDegrees.x, 1.0F, -360.0F, 360.0F,
                                      "%.0f deg");
                    ImGui::DragFloat3("Scale", &object->scale.x, 0.02F, 0.05F, 20.0F, "%.2f");
                } else {
                    ImGui::DragFloat2("위치 XY", &object->position.x, 0.05F, -100.0F, 100.0F, "%.2f");
                    ImGui::DragFloat("회전 Z", &object->rotationDegrees.z, 1.0F, -360.0F, 360.0F, "%.0f deg");
                    ImGui::DragFloat2("Scale", &object->scale.x, 0.02F, 0.05F, 20.0F, "%.2f");
                }
                ImGui::ColorEdit3("Color", &object->color.x);
                ImGui::Checkbox("표시", &object->visible);
                const char *layerNames[] = {"0 · 배경", "1 · 월드", "2 · 캐릭터", "3 · 효과", "4 · UI"};
                int layer = std::clamp(object->layer, 0, 4);
                if (ImGui::BeginCombo("레이어", layerNames[layer])) {
                    for (int index = 0; index < 5; ++index) {
                        if (ImGui::Selectable(layerNames[index], layer == index))
                            object->layer = index;
                    }
                    ImGui::EndCombo();
                }
                ImGui::DragFloat("Move speed", &object->movementSpeed, 0.1F, 0.0F, 100.0F, "%.1f units/s");
                if (object->kind == ObjectKind::Sprite2D) {
                    ImGui::SeparatorText("물리");
                    ImGui::Checkbox("물리 사용", &object->physicsEnabled);
                    if (object->physicsEnabled) {
                        ImGui::Checkbox("고정 오브젝트", &object->isStatic);
                        if (!object->isStatic)
                            ImGui::DragFloat("질량", &object->mass, 0.05F, 0.1F, 100.0F, "%.2f");
                        ImGui::DragFloat("중력 비율", &object->gravityScale, 0.05F, 0.0F, 10.0F, "%.2f");
                    }
                }
                ImGui::SeparatorText("Behavior blocks");
                ImGui::TextColored(ImVec4(0.35F, 0.75F, 0.95F, 1.0F), "When Play is pressed");
                if (ImGui::Button(object->spinWhenPlaying ? "Remove: rotate continuously"
                                                          : "+ Add: rotate continuously")) {
                    object->spinWhenPlaying = !object->spinWhenPlaying;
                }
                ImGui::TextDisabled("The block runs until Stop is pressed.");
                ImGui::Spacing();
                if (ImGui::Button("Delete object", ImVec2(-1.0F, 28.0F)))
                    scene_.deleteSelected();
            }
        }
        ImGui::End();
    }
}

void Application::drawCodePanel() {
    if (!ImGui::BeginTabBar("CodeEditorModes"))
        return;
    if (ImGui::BeginTabItem("게임 스크립트")) {
        drawLegacyCodePanel();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Samat 코어")) {
        drawLanguageCorePanel();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
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
        ImGui::InputTextMultiline("##Samat", &codeBuffer_, ImVec2(-1.0F, editorHeight));
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
    if (ImGui::SmallButton(languageKoreanSyntax_ ? "문법: 訓C正音" : "Syntax: Code")) {
        Program program;
        Diagnostic diagnostic;
        if ((languageKoreanSyntax_ ? parseKorean(languageCodeBuffer_, program, diagnostic)
                                   : parseCode(languageCodeBuffer_, program, diagnostic))) {
            try {
                languageCodeBuffer_ = languageKoreanSyntax_ ? renderCode(program) : renderKorean(program);
                languageKoreanSyntax_ = !languageKoreanSyntax_;
                languageStatus_ = "같은 AST의 구문 표현을 변환했어요.";
            } catch (const std::exception &error) {
                languageStatus_ = error.what();
            }
        } else
            languageStatus_ = diagnostic.message;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Code / Korean Syntax → same JM AST");
    auto parseEditorProgram = [&](Program &program, Diagnostic &diagnostic) {
        return languageKoreanSyntax_ ? parseKorean(languageCodeBuffer_, program, diagnostic)
                                     : parseCode(languageCodeBuffer_, program, diagnostic);
    };
    ImGui::TextDisabled("독립 Samat · Parser → AST → Runtime · 엔진 장면 없이 계산 코드 실행");
    ImGui::SameLine();
    if (ImGui::SmallButton("기본 코드")) {
        languageCodeBuffer_ = languageStarterCode;
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
        languageStatus_ =
            languageCompiled_ ? "컴파일 성공 · 문법을 JM AST로 변환했어요." : diagnostic.message;
        languageOutput_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("실행")) {
        Program program;
        Diagnostic diagnostic;
        if (!parseEditorProgram(program, diagnostic)) {
            languageCompiled_ = false;
            languageStatus_ = diagnostic.message;
            languageOutput_.clear();
        } else {
            try {
                RunOptions options;
                options.instructionBudget = 100'000;
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
                const ExecutionResult result = execute(program, options);
                languageCompiled_ = true;
                languageStatus_ =
                    "실행 완료 · " + std::to_string(result.instructionsExecuted) + "개 명령 처리";
                languageOutput_.clear();
                for (const std::string &line : result.output)
                    languageOutput_ += line + '\n';
                if (!options.entryFunction.empty())
                    languageOutput_ += "결과: " + result.returnValue.toString() + "\n";
            } catch (const std::exception &error) {
                languageCompiled_ = false;
                languageStatus_ = error.what();
                languageOutput_.clear();
            }
        }
    }
    ImGui::SameLine();
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
    if (!ir::LLVMBackend::available())
        ImGui::TextDisabled("LLVM 개발 패키지가 없어 LLVM JIT / IR / AOT가 비활성화돼 있어요.");

    const float helpHeight = 92.0F;
    const float resultHeight = languageOutput_.empty()
                                   ? 34.0F
                                   : (languageOutput_.find("JM IR") != std::string::npos ? 190.0F : 90.0F);
    const float editorHeight =
        std::max(120.0F, ImGui::GetContentRegionAvail().y - helpHeight - resultHeight - 54.0F);
    LanguageCompletionContext completion;
    completion.words = {"let",    "const",  "fn",       "if",   "else",  "while", "for", "in",
                        "return", "break",  "continue", "true", "false", "null",  "and", "or",
                        "not",    "print",  "assert",   "len",  "abs",   "min",   "max", "round",
                        "floor",  "ceil",   "sqrt",     "pow",  "sin",   "cos",   "tan", "vector2",
                        "color",  "append", "push",     "pop",  "clear", "length"};
    auto metadataRegistry = engineNativeFunctions();
    auto metadataEntries = metadataRegistry.allMetadata();
    for (const auto &entry : metadataEntries)
        completion.words.push_back(entry.displayName);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways;
    ImGui::InputTextMultiline("##jm-language-core", &languageCodeBuffer_, ImVec2(-1.0F, editorHeight), flags,
                              languageCompletionCallback, &completion);
    ImGui::TextDisabled("입력 도우미 · Ctrl+Space 또는 Tab으로 자동완성");
    if (!completion.matches.empty() && !completion.prefix.empty()) {
        for (const std::string &suggestion : completion.matches) {
            ImGui::SameLine();
            if (ImGui::SmallButton(suggestion.c_str())) {
                const std::size_t start = static_cast<std::size_t>(
                    std::clamp(completion.wordStart, 0, static_cast<int>(languageCodeBuffer_.size())));
                const std::size_t end =
                    static_cast<std::size_t>(std::clamp(completion.cursor, static_cast<int>(start),
                                                        static_cast<int>(languageCodeBuffer_.size())));
                languageCodeBuffer_.replace(start, end - start, suggestion);
            }
        }
    }
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
    if (!languageStatus_.empty())
        ImGui::TextWrapped("%s", languageStatus_.c_str());
    if (!languageOutput_.empty()) {
        ImGui::BeginChild("SamatOutput", ImVec2(-1.0F, resultHeight - 20.0F), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(languageOutput_.c_str());
        ImGui::EndChild();
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
bool Application::applyLanguageEdit(std::string source, bool korean) {
    if (!languageRuntime_) {
        languageStatus_ = "Samat Play를 먼저 실행하세요.";
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
        scriptStatus_ = "Samat 이벤트 실행을 시작했어요.";
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
