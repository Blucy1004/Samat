#pragma once

#include <SDL3/SDL.h>

#include "JMEngine/Scene/Scene.hpp"
#include "JMEngine/Core/SamatStudioSupport.hpp"
#include "JMEngine/Script/EngineScriptAPI.hpp"
#include "JMEngine/Script/Script.hpp"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace jm {

class Renderer;

struct ApplicationConfig {
    std::string title{"Samat Studio"};
    int width{1280};
    int height{720};
};

class Application {
  public:
    explicit Application(ApplicationConfig config = {});
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    std::size_t run(std::size_t maximumFrames = 0);
    void startLanguagePlay(std::string source, bool korean = false,
                           EngineScriptBackend backend = EngineScriptBackend::Interpreter);
    void loadHaerye(std::string source);
    bool applyLanguageEdit(std::string source, bool korean = false);
    void stopPlay();
    bool isPlaying() const { return playing_; }
    const Scene &scene() const { return scene_; }

  private:
    void processEvents();
    void update(float deltaSeconds);
    void render();
    void drawEditorUI();
    void drawCodePanel();
    void drawLegacyCodePanel();
    void drawLanguageCorePanel();
    void captureViewport();
    void set2DMode(bool enabled);
    void saveProject();
    void openProject();
    void createProject();
    void togglePlaying();
    void shutdown() noexcept;

    ApplicationConfig config_;
    SDL_Window *window_{nullptr};
    SDL_GLContext glContext_{nullptr};
    std::unique_ptr<Renderer> renderer_;
    Scene scene_;
    ScriptDocument script_;
    std::string codeBuffer_;
    std::string koreanBuffer_;
    std::string languageCodeBuffer_;
    std::string scriptStatus_;
    std::string languageStatus_;
    std::string languageOutput_;
    std::string languageFilePath_;
    std::string languagePathInput_;
    std::string languagePendingFilePath_;
    std::string languageKeySearch_;
    std::string languageDiagnostic_;
    studio::TextRange languageKeyRange_{};
    studio::InterpreterRun languageRun_;
    std::uint64_t languageLastRunGeneration_{};
    std::size_t languageErrorLine_{};
    ScriptProgram compiledScript_;
    std::optional<Scene> playSnapshot_;
    float viewportX_{0.0F};
    float viewportY_{0.0F};
    float viewportWidth_{0.0F};
    float viewportHeight_{0.0F};
    std::filesystem::path projectRoot_;
    std::string projectPathInput_;
    std::string projectName_;
    std::string projectId_;
    std::string projectStatus_;
    bool running_{false};
    float cameraYaw_{0.0F};
    float cameraPitch_{0.35F};
    float cameraDistance_{4.2F};
    float cameraPanX2D_{0.0F};
    float cameraPanY2D_{0.0F};
    float zoom2D_{1.0F};
    float scriptAccumulator_{0.0F};
    bool twoDimensional_{true};
    bool dragging_{false};
    bool playing_{false};
    bool projectDialogOpen_{false};
    bool codeMode_{false};
    bool koreanEditMode_{false};
    bool languageCompiled_{false};
    bool languageKoreanSyntax_{false};
    bool languageBeginnerMode_{true};
    bool languageDirty_{false};
    bool languageKeyRangeValid_{false};
    bool languagePlayEnabled_{false};
    int languagePlayBackend_{};
    int languagePendingDocumentAction_{};
    std::unique_ptr<EngineEventRuntime> languageRuntime_;
    bool spacePressedThisFrame_{false};
    std::unordered_set<std::string> keysPressedThisFrame_;
    bool tutorialCodeViewed_{false};
    bool tutorialPlayRun_{false};
    bool viewportVisible_{false};
    bool viewportHovered_{false};
    int activeWorkspace_{0};
    int requestedWorkspace_{-1};
    bool imguiContextReady_{false};
    bool imguiPlatformReady_{false};
    bool imguiRendererReady_{false};
};

} // namespace jm
