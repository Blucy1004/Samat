#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "Samat/EditorSupport.hpp"
#include "Samat/Haerye.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace {
using namespace jm::script;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t windowClass[] = L"SamatStudioWindow";
constexpr int idCategories = 1001;
constexpr int idExamples = 1002;
constexpr int idEditor = 1003;
constexpr int idOutput = 1004;
constexpr int idOpen = 1005;
constexpr int idSave = 1006;
constexpr int idCheck = 1007;
constexpr int idRun = 1008;
constexpr int idOutputLabel = 1009;
constexpr int idConvert = 1010;
constexpr int idFindToggle = 1011;
constexpr int idNew = 1012;
constexpr int idGutter = 1013;
constexpr int idFindInput = 1014;
constexpr int idReplaceInput = 1015;
constexpr int idFindNext = 1016;
constexpr int idReplace = 1017;
constexpr int idReplaceAll = 1018;
constexpr int idFindClose = 1019;
constexpr int idCursorLabel = 1020;
constexpr int idSidebarToggle = 1021;
constexpr int idExamplesFilter = 1022;
constexpr int idCodePage = 1023;
constexpr int idRuntimePage = 1024;
constexpr int idRefreshVariables = 1025;
constexpr int idOpenHaerye = 1026;
constexpr UINT_PTR runTimer = 1;
constexpr UINT_PTR highlightTimer = 2;
constexpr UINT_PTR engineTimer = 3;
constexpr UINT webMessageDispatch = WM_APP + 31;

constexpr COLORREF backgroundColor = RGB(34, 32, 49);
constexpr COLORREF panelColor = RGB(46, 43, 65);
constexpr COLORREF raisedColor = RGB(58, 54, 80);
constexpr COLORREF borderColor = RGB(88, 79, 121);
constexpr COLORREF foregroundColor = RGB(242, 239, 250);
constexpr COLORREF mutedColor = RGB(173, 166, 198);
constexpr COLORREF accentColor = RGB(189, 166, 255);
constexpr COLORREF selectedColor = RGB(73, 62, 105);
constexpr COLORREF stageColor = RGB(247, 245, 251);

struct Category {
    const wchar_t *name;
    const wchar_t *hint;
};

struct Example {
    const wchar_t *title;
    const wchar_t *category;
    const wchar_t *summary;
    const wchar_t *file;
};

constexpr std::array<Category, 8> categories{{
    {L"시작하기", L"Samat의 첫 코드"},
    {L"함수와 계산", L"함수 선언과 값 반환"},
    {L"조건과 반복", L"조건문, 범위, 반복문"},
    {L"訓C正音", L"한국어로 작성하는 코드"},
    {L"콘솔 입력", L"입력을 받는 작은 프로그램"},
    {L"네이티브", L"x64 백엔드 예제"},
    {L"문자열", L"문자열 안에 값을 표현"},
    {L"게임", L"해례 장면과 게임 로직"},
}};

constexpr std::array<Example, 8> examples{{
    {L"Hello, Samat", L"시작하기", L"화면에 첫 문장을 출력합니다", L"hello.st"},
    {L"함수 맛보기", L"함수와 계산", L"factorial 함수와 결과 출력", L"functions.st"},
    {L"재귀 함수", L"함수와 계산", L"정수 함수의 네이티브 컴파일", L"native-factorial.st"},
    {L"조건과 반복", L"조건과 반복", L"if와 범위 반복으로 합계를 계산", L"conditions-loops.st"},
    {L"한국어 함수", L"訓C正音", L"같은 언어 기능을 한국어 문법으로 표현", L"functions-korean.st"},
    {L"대화형 계산기", L"콘솔 입력", L"readLine으로 입력받아 사칙 연산", L"calculator.st"},
    {L"문자열 보간", L"문자열", L"f-문자열에 변수와 식을 넣어 출력", L"string-interpolation.st"},
    {L"2D 파쿠르", L"게임", L"점프 거리와 발판 구간을 계산", L"parkour.st"},
}};

HWND mainWindow{};
ComPtr<ICoreWebView2Environment> webEnvironment;
ComPtr<ICoreWebView2Controller> webController;
ComPtr<ICoreWebView2> webView;
bool webReady{};
bool webMode{true};
HWND categoryList{};
HWND exampleList{};
HWND editor{};
HWND output{};
HWND titleLabel{};
HWND pathLabel{};
HWND categoryCaption{};
HWND examplesCaption{};
HWND outputCaption{};
HWND openButton{};
HWND openHaeryeButton{};
HWND newButton{};
HWND saveButton{};
HWND checkButton{};
HWND runButton{};
HWND convertButton{};
HWND findButton{};
HWND gutter{};
HWND findInput{};
HWND replaceInput{};
HWND findNextButton{};
HWND replaceButton{};
HWND replaceAllButton{};
HWND findCloseButton{};
HWND cursorLabel{};
HWND sidebarToggleButton{};
HWND examplesFilter{};
HWND codePageButton{};
HWND runtimePageButton{};
HWND runtimeStage{};
HWND runtimeInspectorCaption{};
HWND refreshVariablesButton{};
HMODULE richEditModule{};
HFONT uiFont{};
HFONT editorFont{};
HBRUSH backgroundBrush{};
HBRUSH panelBrush{};
HBRUSH raisedBrush{};
HBRUSH stageBrush{};
HFONT cardTitleFont{};
HFONT cardDetailFont{};
std::filesystem::path examplesPath;
std::filesystem::path currentPath;
std::filesystem::path haeryePath;
jm::HaeryeDocument runtimeScene;
bool runtimeSceneLoaded{};
std::vector<const Example *> visibleExamples;
samat::editor::InterpreterRun interpreter;
bool hasActiveRun{};
bool documentDirty{};
bool suppressEditorChange{};
bool findBarVisible{};
bool findReplaceVisible{};
bool sidebarVisible{true};
bool draggingOutputSplitter{};
bool draggingSidebarSplitter{};
int outputPanelHeight{190};
int sidebarWidthSetting{405};
std::wstring exampleFilterText;
enum class Surface { Code, Korean };
Surface currentSurface{Surface::Code};
enum class WorkspacePage { Code, Runtime };
WorkspacePage workspacePage{WorkspacePage::Code};

struct RuntimeVariableRow {
    std::string name;
    Type type{Type::Any};
    HWND nameLabel{};
    HWND valueEdit{};
    HWND currentLabel{};
    std::string currentValue;
};
std::vector<RuntimeVariableRow> runtimeVariables;
int runtimeVariablesScroll{};

struct EngineSprite {
    std::int64_t id{};
    std::string name;
    std::string color;
    std::string source;
    double x{}, y{}, width{48.0}, height{48.0};
    std::unique_ptr<ExecutionSession> session;
};
std::vector<EngineSprite> engineSprites;
std::unordered_set<std::wstring> engineKeys;
std::filesystem::path currentProjectPath;
bool engineRunning{};
bool engineProjectMode{};
bool engineProjectDirty{};
std::wstring standaloneSourceSnapshot;
std::filesystem::path standalonePathSnapshot;
Surface standaloneSurfaceSnapshot{Surface::Code};
bool standaloneDirtySnapshot{};
bool standaloneSnapshotValid{};
double engineDeltaTime{1.0 / 60.0};
std::chrono::steady_clock::time_point engineLastFrame{};

void postWebMessage(std::wstring message);
void setText(HWND control, std::wstring_view text);
void updatePathLabel();
void publishEditorState();
void publishRuntimeOutput();
void newDocument();

void layoutControls(HWND window);
void refreshRuntimeVariables(bool preserveValues = true);

std::wstring toWide(std::string_view value) {
    if (value.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0)
        return L"(텍스트를 표시할 수 없습니다)";
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed);
    return result;
}

std::string toUtf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed,
                        nullptr, nullptr);
    return result;
}

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(std::max(0, length)));
    return value;
}

std::filesystem::path studioWebDirectory() {
#ifdef SAMAT_STUDIO_WEB_DIR
    const std::filesystem::path buildAssets(SAMAT_STUDIO_WEB_DIR);
    if (std::filesystem::exists(buildAssets / L"index.html"))
        return buildAssets;
#endif
    std::array<wchar_t, 32768> module{};
    const DWORD size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    return size > 0 && size < module.size() ? std::filesystem::path(module.data()).parent_path() / L"studio-web"
                                            : std::filesystem::current_path() / L"studio-web";
}

std::string jsonQuoted(std::string_view value) {
    std::string result{"\""};
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 15];
            } else result.push_back(static_cast<char>(ch));
        }
    }
    result += '"';
    return result;
}

std::optional<std::filesystem::path> chooseProjectPath(bool save) {
    std::array<wchar_t, 32768> path{};
    if (!currentProjectPath.empty())
        wcsncpy_s(path.data(), path.size(), currentProjectPath.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = mainWindow;
    dialog.lpstrFilter = L"Samat Studio Project (*.samatproj)\0*.samatproj\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"samatproj";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL selected = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!selected)
        return std::nullopt;
    return std::filesystem::path(path.data());
}

bool readProjectPacket(std::wstring_view packet, std::wstring &projectName, std::vector<EngineSprite> &sprites,
                       std::wstring &error) {
    std::size_t at = 0;
    auto line = [&]() -> std::optional<std::wstring_view> {
        const auto end = packet.find(L'\n', at);
        if (end == std::wstring_view::npos)
            return std::nullopt;
        auto result = packet.substr(at, end - at);
        if (!result.empty() && result.back() == L'\r')
            result.remove_suffix(1);
        at = end + 1;
        return result;
    };
    auto text = [&]() -> std::optional<std::wstring_view> {
        const auto lengthLine = line();
        if (!lengthLine)
            return std::nullopt;
        std::size_t length{};
        try {
            length = static_cast<std::size_t>(std::stoull(std::wstring(*lengthLine)));
        } catch (...) {
            return std::nullopt;
        }
        if (length > packet.size() - at)
            return std::nullopt;
        const auto result = packet.substr(at, length);
        at += length;
        if (at < packet.size() && packet[at] == L'\r')
            ++at;
        if (at < packet.size() && packet[at] == L'\n')
            ++at;
        return result;
    };
    auto number = [&](double &value) {
        const auto item = line();
        if (!item)
            return false;
        try {
            std::size_t used{};
            value = std::stod(std::wstring(*item), &used);
            return used == item->size() && std::isfinite(value);
        } catch (...) {
            return false;
        }
    };
    if (line() != std::optional<std::wstring_view>(L"SMP1")) {
        error = L"지원하지 않는 프로젝트 형식입니다.";
        return false;
    }
    const auto name = text();
    const auto countLine = line();
    std::size_t count{};
    try {
        if (!countLine)
            throw std::runtime_error("count");
        count = static_cast<std::size_t>(std::stoull(std::wstring(*countLine)));
    } catch (...) {
        error = L"프로젝트의 오브젝트 수가 잘못되었습니다.";
        return false;
    }
    if (!name || count > 500) {
        error = L"프로젝트 헤더가 올바르지 않습니다.";
        return false;
    }
    projectName.assign(*name);
    sprites.clear();
    sprites.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto idLine = line();
        EngineSprite sprite;
        try {
            if (!idLine)
                throw std::runtime_error("id");
            sprite.id = std::stoll(std::wstring(*idLine));
        } catch (...) {
            error = L"오브젝트 식별자가 잘못되었습니다.";
            return false;
        }
        const auto spriteName = text();
        if (!spriteName || !number(sprite.x) || !number(sprite.y) || !number(sprite.width) ||
            !number(sprite.height)) {
            error = L"오브젝트 속성을 읽을 수 없습니다.";
            return false;
        }
        if (sprite.width <= 0.0 || sprite.height <= 0.0) {
            error = L"오브젝트 크기는 0보다 커야 합니다.";
            return false;
        }
        sprite.name = toUtf8(*spriteName);
        const auto color = text();
        const auto source = text();
        if (!color || !source) {
            error = L"오브젝트 색상 또는 코드를 읽을 수 없습니다.";
            return false;
        }
        sprite.color = toUtf8(*color);
        sprite.source = toUtf8(*source);
        sprites.push_back(std::move(sprite));
    }
    return true;
}

std::string engineStatePacket() {
    std::ostringstream out;
    out.precision(12);
    for (const auto &sprite : engineSprites)
        out << sprite.id << '|' << sprite.x << '|' << sprite.y << '\n';
    return out.str();
}

void syncSpriteFromSession(EngineSprite &sprite) {
    if (!sprite.session)
        return;
    const auto variables = sprite.session->inspect();
    for (const auto &[name, value] : variables) {
        if (name != "x" && name != "y")
            continue;
        try {
            const auto separator = value.find(": ");
            const double number = std::stod(separator == std::string::npos ? value : value.substr(separator + 2));
            if (std::isfinite(number))
                (name == "x" ? sprite.x : sprite.y) = number;
        } catch (...) {
        }
    }
}

void stopEngine() {
    engineRunning = false;
    if (mainWindow)
        KillTimer(mainWindow, engineTimer);
    for (auto &sprite : engineSprites)
        sprite.session.reset();
    engineKeys.clear();
}

void beginEngineProjectMode() {
    if (!engineProjectMode) {
        standaloneSourceSnapshot = windowText(editor);
        standalonePathSnapshot = currentPath;
        standaloneSurfaceSnapshot = currentSurface;
        standaloneDirtySnapshot = documentDirty;
        standaloneSnapshotValid = true;
    }
    engineProjectMode = true;
}

void returnToSingleFileMode() {
    stopEngine();
    engineProjectMode = false;
    if (standaloneSnapshotValid) {
        suppressEditorChange = true;
        setText(editor, standaloneSourceSnapshot);
        suppressEditorChange = false;
        currentPath = standalonePathSnapshot;
        currentSurface = standaloneSurfaceSnapshot;
        documentDirty = standaloneDirtySnapshot;
        standaloneSnapshotValid = false;
        updatePathLabel();
        refreshRuntimeVariables(true);
    }
    publishEditorState();
    publishRuntimeOutput();
}

bool startEngine(std::wstring_view packet) {
    stopEngine();
    std::wstring name, error;
    std::vector<EngineSprite> parsed;
    if (!readProjectPacket(packet, name, parsed, error)) {
        postWebMessage(L"engine-status\n오류\n" + error);
        return false;
    }
    engineSprites = std::move(parsed);
    for (auto &sprite : engineSprites) {
        Program program;
        Diagnostic diagnostic;
        if (!parseCode(sprite.source, program, diagnostic) && !parseKorean(sprite.source, program, diagnostic)) {
            stopEngine();
            postWebMessage(L"engine-status\n오류\n" + toWide(sprite.name) + L" 코드 오류: " +
                           toWide(diagnostic.message));
            return false;
        }
        RunOptions options;
        options.instructionBudget = 25'000;
        for (const auto &[name, coordinate] : {std::pair<std::string_view, double>{"x", sprite.x},
                                              {"y", sprite.y}}) {
            const auto declaration = std::find_if(program.statements.begin(), program.statements.end(),
                                                  [&](const Statement &item) {
                return item.kind == Statement::Kind::Variable && item.name == name;
            });
            if (declaration == program.statements.end())
                continue;
            Type coordinateType = declaration->declaredType;
            if (coordinateType == Type::Any && declaration->expression &&
                declaration->expression->kind == Expression::Kind::Literal)
                coordinateType = declaration->expression->literal.type();
            if (coordinateType == Type::Int)
                options.initialValues[std::string(name)] = Value(static_cast<std::int64_t>(std::llround(coordinate)));
            else if (coordinateType == Type::Float || coordinateType == Type::Any)
                options.initialValues[std::string(name)] = Value(coordinate);
            else {
                stopEngine();
                postWebMessage(L"engine-status\n오류\n" + toWide(sprite.name) + L"의 " + toWide(name) +
                               L" 좌표는 숫자 변수여야 합니다.");
                return false;
            }
        }
        EngineSprite *context = &sprite;
        HostFunction host = [context](const std::string &function, const std::vector<Value> &args,
                                      const std::vector<std::string> &) -> Value {
            if (function == "deltaTime")
                return Value(engineDeltaTime);
            if (function == "keyDown" && args.size() == 1) {
                const auto key = std::get_if<std::string>(&args[0].data);
                if (!key)
                    throw std::runtime_error("keyDown expects a String key.");
                return Value(engineKeys.contains(toWide(*key)));
            }
            if (function == "touching" && args.size() == 1) {
                const auto name = std::get_if<std::string>(&args[0].data);
                if (!name)
                    throw std::runtime_error("touching expects an object name.");
                const auto other = std::find_if(engineSprites.begin(), engineSprites.end(), [&](const auto &item) {
                    return &item != context && item.name == *name;
                });
                if (other == engineSprites.end())
                    return Value(false);
                const bool overlap = std::abs(context->x - other->x) * 2.0 < context->width + other->width &&
                                     std::abs(context->y - other->y) * 2.0 < context->height + other->height;
                return Value(overlap);
            }
            throw std::out_of_range("Unknown Studio engine function.");
        };
        try {
            sprite.session = std::make_unique<ExecutionSession>(std::move(program), std::move(options), host);
            (void)sprite.session->dispatch("start");
            syncSpriteFromSession(sprite);
        } catch (const std::exception &exception) {
            stopEngine();
            postWebMessage(L"engine-status\n오류\n" + toWide(sprite.name) + L": " + toWide(exception.what()));
            return false;
        }
    }
    engineRunning = true;
    engineLastFrame = std::chrono::steady_clock::now();
    SetTimer(mainWindow, engineTimer, 16, nullptr);
    postWebMessage(L"engine-state\n" + toWide(engineStatePacket()));
    postWebMessage(L"engine-status\n실행 중\n모든 오브젝트의 on start / on update 이벤트가 실행 중입니다.");
    return true;
}

void updateEngine() {
    if (!engineRunning)
        return;
    const auto now = std::chrono::steady_clock::now();
    engineDeltaTime = std::clamp(std::chrono::duration<double>(now - engineLastFrame).count(), 0.001, 0.1);
    engineLastFrame = now;
    try {
        for (auto &sprite : engineSprites) {
            if (sprite.session) {
                (void)sprite.session->dispatch("update");
                syncSpriteFromSession(sprite);
            }
        }
        postWebMessage(L"engine-state\n" + toWide(engineStatePacket()));
    } catch (const std::exception &exception) {
        stopEngine();
        postWebMessage(L"engine-status\n오류\n" + toWide(exception.what()));
    }
}

void postWebMessage(std::wstring message) {
    if (webReady && webView)
        webView->PostWebMessageAsString(message.c_str());
}

void postSceneToWeb() {
    if (!runtimeSceneLoaded) {
        postWebMessage(L"scene-clear");
        return;
    }
    std::string json = "{\"name\":" + jsonQuoted(runtimeScene.name) + ",\"background\":[" +
                       std::to_string(runtimeScene.backgroundColor.r) + "," +
                       std::to_string(runtimeScene.backgroundColor.g) + "," +
                       std::to_string(runtimeScene.backgroundColor.b) + "]";
    if (runtimeScene.gameMode == jm::HaeryeGameMode::Platformer)
        json += ",\"game\":\"platformer\"";
    json += ",\"objects\":[";
    for (std::size_t index = 0; index < runtimeScene.objects.size(); ++index) {
        const auto &object = runtimeScene.objects[index];
        if (index) json += ',';
        json += "{\"name\":" + jsonQuoted(object.name);
        json += ",\"shape\":" + jsonQuoted(object.shape == jm::HaeryeShape::Circle ? "circle" : "rectangle");
        if (object.role) {
            constexpr std::array<std::string_view, 4> roles{"player", "platform", "goal", "hazard"};
            json += ",\"role\":" + jsonQuoted(std::string(roles[static_cast<std::size_t>(*object.role)]));
        }
        if (object.position)
            json += ",\"position\":[" + std::to_string((*object.position)[0]) + "," +
                    std::to_string((*object.position)[1]) + "]";
        if (object.size)
            json += ",\"size\":[" + std::to_string((*object.size)[0]) + "," +
                    std::to_string((*object.size)[1]) + "]";
        if (object.color)
            json += ",\"color\":[" + std::to_string(object.color->r) + "," +
                    std::to_string(object.color->g) + "," + std::to_string(object.color->b) + "]";
        json += '}';
    }
    json += "]}";
    postWebMessage(L"scene\n" + toWide(json));
}

void publishRuntimeVariables();
void publishEditorState();
void publishRuntimeOutput();
void handleWebMessage(std::wstring_view message);
void updatePathLabel();
void newDocument();

std::wstring loadStudioHtml() {
    const auto directory = studioWebDirectory();
    std::ifstream htmlFile(directory / L"index.html", std::ios::binary);
    std::ifstream cssFile(directory / L"studio.css", std::ios::binary);
    std::ifstream jsFile(directory / L"studio.js", std::ios::binary);
    if (!htmlFile || !cssFile || !jsFile)
        return {};
    std::string html((std::istreambuf_iterator<char>(htmlFile)), std::istreambuf_iterator<char>());
    std::string css((std::istreambuf_iterator<char>(cssFile)), std::istreambuf_iterator<char>());
    std::string js((std::istreambuf_iterator<char>(jsFile)), std::istreambuf_iterator<char>());
    const auto link = html.find("<link rel=\"stylesheet\" href=\"studio.css\">");
    if (link == std::string::npos)
        return {};
    html.replace(link, std::string("<link rel=\"stylesheet\" href=\"studio.css\">").size(),
                 "<style>\n" + css + "\n</style>");
    const auto script = html.find("<script src=\"studio.js\"></script>");
    if (script == std::string::npos)
        return {};
    html.replace(script, std::string("<script src=\"studio.js\"></script>").size(),
                 "<script>\n" + js + "\n</script>");
    return toWide(html);
}

void initializeWebView(HWND window) {
    std::array<wchar_t, 32768> localAppData{};
    const DWORD appDataLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
                                                         static_cast<DWORD>(localAppData.size()));
    const auto userData = (appDataLength > 0 && appDataLength < localAppData.size()
                               ? std::filesystem::path(localAppData.data())
                               : std::filesystem::temp_directory_path()) /
                          L"Samat" / L"Studio" / L"WebView2";
    std::error_code ignored;
    std::filesystem::create_directories(userData, ignored);
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [window](HRESULT error, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(error) || !environment) {
                    MessageBoxW(window, L"Microsoft Edge WebView2 Runtime을 사용할 수 없습니다.",
                                L"Samat Studio 시작 실패", MB_OK | MB_ICONERROR);
                    return error;
                }
                webEnvironment = environment;
                return environment->CreateCoreWebView2Controller(
                    window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [window](HRESULT createError, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(createError) || !controller)
                                return createError;
                            webController = controller;
                            controller->get_CoreWebView2(&webView);
                            RECT bounds{};
                            GetClientRect(window, &bounds);
                            controller->put_Bounds(bounds);
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webView->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                            }
                            webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [window](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                                        LPWSTR raw{};
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            auto *message = new std::wstring(raw);
                                            PostMessageW(window, webMessageDispatch, 0,
                                                         reinterpret_cast<LPARAM>(message));
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            const auto page = loadStudioHtml();
                            if (page.empty()) {
                                webView->PostWebMessageAsString(L"status\n화면 오류\nStudio 웹 화면 파일을 찾지 못했습니다.");
                                return E_FAIL;
                            }
                            return webView->NavigateToString(page.c_str());
                        }).Get());
            }).Get());
    if (FAILED(result))
        MessageBoxW(window, L"WebView2 초기화를 시작하지 못했습니다.", L"Samat Studio", MB_OK | MB_ICONERROR);
}

void setText(HWND control, std::wstring_view text) {
    SetWindowTextW(control, std::wstring(text).c_str());
}

std::filesystem::path discoverExamplesPath() {
    std::array<wchar_t, 32768> module{};
    const DWORD size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (size > 0 && size < module.size()) {
        const auto packaged = std::filesystem::path(module.data()).parent_path().parent_path() /
                              L"examples" / L"Samat" / L"v1.0";
        if (std::filesystem::exists(packaged / L"hello.st"))
            return packaged;
    }
#ifdef SAMAT_STUDIO_EXAMPLES_DIR
    const std::filesystem::path sourceTree(SAMAT_STUDIO_EXAMPLES_DIR);
    if (std::filesystem::exists(sourceTree / L"hello.st"))
        return sourceTree;
#endif
    return std::filesystem::current_path() / L"examples" / L"Samat" / L"v1.0";
}

std::vector<const Example *> examplesFor(std::wstring_view category) {
    std::vector<const Example *> result;
    for (const auto &example : examples)
        if ((category == L"모든 예제" || category == example.category) &&
            (exampleFilterText.empty() ||
             CompareStringOrdinal(example.title, -1, exampleFilterText.c_str(), -1, TRUE) == CSTR_EQUAL ||
             std::wstring_view(example.title).find(exampleFilterText) != std::wstring_view::npos ||
             std::wstring_view(example.summary).find(exampleFilterText) != std::wstring_view::npos))
            result.push_back(&example);
    return result;
}

void refreshExampleList(int selected = 0) {
    const int categoryIndex = static_cast<int>(SendMessageW(categoryList, LB_GETCURSEL, 0, 0));
    const std::wstring category = categoryIndex == 0
                                      ? L"모든 예제"
                                      : (categoryIndex > 0 && categoryIndex <= static_cast<int>(categories.size())
                                             ? categories[static_cast<std::size_t>(categoryIndex - 1)].name
                                             : L"모든 예제");
    visibleExamples = examplesFor(category);
    SendMessageW(exampleList, LB_RESETCONTENT, 0, 0);
    for (const auto *example : visibleExamples)
        SendMessageW(exampleList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(example->title));
    if (!visibleExamples.empty()) {
        const int safeSelected = std::clamp(selected, 0, static_cast<int>(visibleExamples.size()) - 1);
        SendMessageW(exampleList, LB_SETCURSEL, safeSelected, 0);
    }
    InvalidateRect(categoryList, nullptr, TRUE);
    InvalidateRect(exampleList, nullptr, TRUE);
}

void updateExampleFilter() {
    exampleFilterText = windowText(examplesFilter);
    refreshExampleList();
}

bool saveCurrent();
void openFile();
void openHaeryeFile();
void runSource();
void convertSurface();
bool parseAndCheck(Program &program, std::string &message);

const char *surfaceName(Surface surface) { return surface == Surface::Code ? "Code Syntax" : "訓C正音"; }

void updatePathLabel() {
    const std::wstring name = currentPath.empty() ? L"새 문서" : currentPath.filename().wstring();
    const std::wstring mode = currentSurface == Surface::Code ? L"Code Syntax" : L"訓C正音";
    const std::wstring scene = haeryePath.empty() ? L"  ·  해례 없음" : L"  ·  ◈ " + haeryePath.filename().wstring();
    setText(pathLabel, mode + L"  ·  " + name + scene + (documentDirty ? L"  •  수정됨" : L""));
    if (convertButton)
        SetWindowTextW(convertButton, currentSurface == Surface::Code ? L"訓C正音으로" : L"Code Syntax로");
}

Surface detectSurface(std::string_view source, Surface fallback) {
    Program candidate;
    Diagnostic diagnostic;
    if (parseCode(std::string(source), candidate, diagnostic))
        return Surface::Code;
    if (parseKorean(std::string(source), candidate, diagnostic))
        return Surface::Korean;
    return fallback;
}

bool parseSurface(std::string_view source, Surface surface, Program &program, Diagnostic &diagnostic) {
    return surface == Surface::Code ? parseCode(std::string(source), program, diagnostic)
                                    : parseKorean(std::string(source), program, diagnostic);
}

bool loadPath(const std::filesystem::path &path) {
    if (documentDirty && !saveCurrent())
        return false;
    std::string source, error;
    if (!samat::editor::loadScriptFile(path, source, error)) {
        MessageBoxW(mainWindow, toWide(error).c_str(), L"파일을 열지 못했습니다", MB_OK | MB_ICONERROR);
        return false;
    }
    currentSurface = detectSurface(source, currentSurface);
    suppressEditorChange = true;
    setText(editor, toWide(source));
    suppressEditorChange = false;
    currentPath = path;
    documentDirty = false;
    updatePathLabel();
    refreshRuntimeVariables(true);
    publishEditorState();
    SetTimer(mainWindow, highlightTimer, 1, nullptr);
    InvalidateRect(gutter, nullptr, TRUE);
    InvalidateRect(runtimeStage, nullptr, TRUE);
    setText(output, L"예제를 열었습니다. 코드를 수정하고 검사하거나 실행할 수 있습니다.");
    setText(outputCaption, L"준비됨");
    return true;
}

bool loadHaeryePath(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        MessageBoxW(mainWindow, L"해례 파일을 읽지 못했습니다.", L"파일 열기", MB_OK | MB_ICONERROR);
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    jm::HaeryeDocument document;
    jm::HaeryeDiagnostic diagnostic;
    if (!jm::parseHaerye(source, document, diagnostic) || !jm::validateHaerye(document, diagnostic)) {
        setText(output, toWide(diagnostic.toString()));
        setText(outputCaption, L"해례 오류");
        MessageBoxW(mainWindow, toWide(diagnostic.toString()).c_str(), L"해례를 읽지 못했습니다", MB_OK | MB_ICONERROR);
        return false;
    }
    runtimeScene = std::move(document);
    runtimeSceneLoaded = true;
    haeryePath = path;
    updatePathLabel();
    postSceneToWeb();
    publishEditorState();
    postWebMessage(L"show-runtime");
    InvalidateRect(runtimeStage, nullptr, TRUE);
    setText(output, runtimeScene.gameMode == jm::HaeryeGameMode::Platformer
                        ? L"플랫폼 게임 장면을 열었습니다. 런타임에서 게임 시작을 누르세요."
                        : L"해례 장면을 런타임에 연결했습니다. Samat 파일과 함께 실행할 수 있습니다.");
    setText(outputCaption, L"장면 준비됨");
    return true;
}

std::string editorSource() { return toUtf8(windowText(editor)); }

void publishEditorState() {
    if (!webReady)
        return;
    const std::wstring source = toWide(editorSource());
    const std::wstring file = currentPath.empty() ? L"새 문서" : currentPath.filename().wstring();
    const std::wstring scene = haeryePath.empty() ? L"" : haeryePath.filename().wstring();
    const std::wstring mode = currentSurface == Surface::Code ? L"Code Syntax" : L"訓C正音";
    postWebMessage(L"state\n" + std::to_wstring(source.size()) + L"\n" + source + L"\n" + file +
                   L"\n" + scene + L"\n" + mode);
}

void publishRuntimeOutput() {
    if (!webReady)
        return;
    const std::wstring text = windowText(output);
    postWebMessage(L"output\n" + windowText(outputCaption) + L"\n" + std::to_wstring(text.size()) +
                   L"\n" + text);
}

std::unordered_map<std::wstring, std::wstring> parseWebValues(std::wstring_view json) {
    std::unordered_map<std::wstring, std::wstring> values;
    std::size_t at = 0;
    const auto skipSpace = [&] { while (at < json.size() && iswspace(json[at])) ++at; };
    const auto readString = [&]() -> std::optional<std::wstring> {
        if (at >= json.size() || json[at++] != L'"') return std::nullopt;
        std::wstring result;
        while (at < json.size() && json[at] != L'"') {
            wchar_t ch = json[at++];
            if (ch == L'\\' && at < json.size()) {
                ch = json[at++];
                if (ch == L'n') result += L'\n';
                else if (ch == L'r') result += L'\r';
                else if (ch == L't') result += L'\t';
                else if (ch == L'u' && at + 4 <= json.size()) {
                    unsigned value{};
                    for (int i = 0; i < 4; ++i) {
                        const wchar_t digit = json[at++];
                        value = value * 16 + (digit >= L'0' && digit <= L'9' ? digit - L'0' :
                                              digit >= L'a' && digit <= L'f' ? digit - L'a' + 10 :
                                              digit >= L'A' && digit <= L'F' ? digit - L'A' + 10 : 0);
                    }
                    result += static_cast<wchar_t>(value);
                } else result += ch;
            } else result += ch;
        }
        if (at >= json.size()) return std::nullopt;
        ++at;
        return result;
    };
    skipSpace();
    if (at >= json.size() || json[at++] != L'{') return values;
    while (at < json.size()) {
        skipSpace();
        if (at < json.size() && json[at] == L'}') break;
        auto key = readString();
        if (!key) break;
        skipSpace();
        if (at >= json.size() || json[at++] != L':') break;
        skipSpace();
        auto value = readString();
        if (!value) break;
        values.emplace(std::move(*key), std::move(*value));
        skipSpace();
        if (at >= json.size() || json[at++] != L',') break;
    }
    return values;
}

void handleWebMessage(std::wstring_view message) {
    if (message == L"ready") {
        webReady = true;
        publishEditorState();
        publishRuntimeVariables();
        postSceneToWeb();
        publishRuntimeOutput();
        return;
    }
    if (message == L"open-st") { openFile(); return; }
    if (message == L"open-hy") { openHaeryeFile(); return; }
    if (message == L"new-file") { newDocument(); publishEditorState(); publishRuntimeOutput(); return; }
    if (message == L"object-mode") { beginEngineProjectMode(); return; }
    if (message == L"single-mode") { returnToSingleFileMode(); return; }
    if (message == L"project-dirty\ntrue") { engineProjectDirty = true; return; }
    if (message == L"project-dirty\nfalse") { engineProjectDirty = false; return; }
    if (message == L"open-project") {
        const auto selected = chooseProjectPath(false);
        if (!selected)
            return;
        std::ifstream input(*selected, std::ios::binary);
        if (!input) {
            postWebMessage(L"engine-status\n오류\n프로젝트 파일을 읽을 수 없습니다.");
            return;
        }
        const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        beginEngineProjectMode();
        currentProjectPath = *selected;
        documentDirty = false;
        postWebMessage(L"project-load\n" + toWide(contents));
        return;
    }
    if (message.starts_with(L"engine-save\n")) {
        const auto packet = message.substr(12);
        auto selected = currentProjectPath.empty() ? chooseProjectPath(true)
                                                   : std::optional<std::filesystem::path>(currentProjectPath);
        if (!selected)
            return;
        std::ofstream outputFile(*selected, std::ios::binary | std::ios::trunc);
        const auto contents = toUtf8(packet);
        if (!outputFile || !outputFile.write(contents.data(), static_cast<std::streamsize>(contents.size()))) {
            postWebMessage(L"engine-status\n저장 오류\n프로젝트를 저장하지 못했습니다.");
            return;
        }
        currentProjectPath = *selected;
        postWebMessage(L"engine-status\n저장됨\n프로젝트를 저장했습니다.");
        return;
    }
    if (message.starts_with(L"engine-run\n")) {
        beginEngineProjectMode();
        (void)startEngine(message.substr(11));
        return;
    }
    if (message.starts_with(L"engine-source\n")) {
        beginEngineProjectMode();
        suppressEditorChange = true;
        setText(editor, message.substr(14));
        suppressEditorChange = false;
        currentSurface = detectSurface(toUtf8(message.substr(14)), currentSurface);
        documentDirty = false;
        updatePathLabel();
        SetTimer(mainWindow, highlightTimer, 1, nullptr);
        return;
    }
    if (message == L"engine-stop") {
        stopEngine();
        postWebMessage(L"engine-status\n정지\n실행을 정지했습니다.");
        return;
    }
    if (message.starts_with(L"engine-key\n")) {
        const auto keyStart = message.find(L'\n', 11);
        if (keyStart == std::wstring_view::npos)
            return;
        const auto phaseStart = message.find(L'\n', keyStart + 1);
        if (phaseStart == std::wstring_view::npos)
            return;
        const std::wstring key(message.substr(11, keyStart - 11));
        const std::wstring phase(message.substr(phaseStart + 1));
        if (phase == L"pressed")
            engineKeys.insert(key);
        else
            engineKeys.erase(key);
        const std::string event = "key." + toUtf8(key) + (phase == L"pressed" ? ".pressed" : ".released");
        try {
            for (auto &sprite : engineSprites)
                if (sprite.session)
                    (void)sprite.session->dispatch(event);
            postWebMessage(L"engine-state\n" + toWide(engineStatePacket()));
        } catch (const std::exception &exception) {
            stopEngine();
            postWebMessage(L"engine-status\n오류\n" + toWide(exception.what()));
        }
        return;
    }
    if (message == L"save") {
        const bool saved = saveCurrent();
        postWebMessage(saved ? L"status\n저장됨\n파일을 저장했습니다." : L"status\n취소됨\n저장을 취소했습니다.");
        publishEditorState();
        return;
    }
    if (message == L"check") {
        Program program;
        std::string detail;
        const bool valid = parseAndCheck(program, detail);
        setText(output, toWide(detail));
        setText(outputCaption, valid ? L"검사 통과" : L"오류");
        publishRuntimeOutput();
        return;
    }
    if (message == L"run") {
        if (hasActiveRun) {
            interpreter.requestStop();
            return;
        }
        runSource();
        return;
    }
    if (message == L"convert") {
        convertSurface();
        refreshRuntimeVariables(true);
        publishEditorState();
        publishRuntimeVariables();
        return;
    }
    if (message == L"refresh") {
        refreshRuntimeVariables(true);
        publishEditorState();
        return;
    }
    if (message.starts_with(L"example\n")) {
        const auto file = message.substr(8);
        const auto found = std::find_if(examples.begin(), examples.end(), [&](const Example &item) {
            return file == item.file;
        });
        if (found != examples.end())
            loadPath(examplesPath / found->file);
        return;
    }
    if (message.starts_with(L"source\n")) {
        suppressEditorChange = true;
        setText(editor, message.substr(7));
        suppressEditorChange = false;
        documentDirty = true;
        updatePathLabel();
        refreshRuntimeVariables(true);
        SetTimer(mainWindow, highlightTimer, 180, nullptr);
        return;
    }
    if (message.starts_with(L"run\n")) {
        if (hasActiveRun) {
            interpreter.requestStop();
            return;
        }
        const auto values = parseWebValues(message.substr(4));
        refreshRuntimeVariables(true);
        for (auto &row : runtimeVariables)
            if (const auto found = values.find(toWide(row.name)); found != values.end())
                setText(row.valueEdit, found->second);
        runSource();
    }
}

bool parseAndCheck(Program &program, std::string &message) {
    Diagnostic parseDiagnostic;
    const auto source = editorSource();
    if (!parseSurface(source, currentSurface, program, parseDiagnostic)) {
        message = "구문 오류";
        if (parseDiagnostic.line > 0)
            message += " · " + std::to_string(parseDiagnostic.line) + "행";
        message += "\n" + parseDiagnostic.message;
        return false;
    }
    std::vector<Diagnostic> diagnostics;
    if (!check(program, diagnostics)) {
        message = "타입 검사 오류";
        for (const auto &diagnostic : diagnostics) {
            message += "\n";
            if (diagnostic.line > 0)
                message += std::to_string(diagnostic.line) + "행: ";
            message += diagnostic.message;
        }
        return false;
    }
    message = "검사 통과 · 구문과 타입이 올바릅니다.";
    return true;
}

Type adjustableType(const Statement &statement) {
    if (statement.declaredType == Type::Any && statement.expression &&
        statement.expression->kind == Expression::Kind::Literal)
        return statement.expression->literal.type();
    return statement.declaredType;
}

bool isAdjustable(Type type) {
    return type == Type::Int || type == Type::Float || type == Type::Bool || type == Type::String;
}

std::string defaultVariableText(const Statement &statement) {
    const Type type = adjustableType(statement);
    if (statement.expression && statement.expression->kind == Expression::Kind::Literal) {
        const auto &value = statement.expression->literal;
        if (value.type() == type || (type == Type::Float && value.type() == Type::Int))
            return value.toString();
    }
    switch (type) {
    case Type::Int: return "0";
    case Type::Float: return "0.0";
    case Type::Bool: return "false";
    case Type::String: return {};
    default: return {};
    }
}

void refreshRuntimeVariables(bool preserveValues) {
    if (!mainWindow || !runtimeStage)
        return;
    std::unordered_map<std::string, std::wstring> saved;
    if (preserveValues)
        for (const auto &row : runtimeVariables)
            if (row.valueEdit)
                saved[row.name] = windowText(row.valueEdit);
    for (auto &row : runtimeVariables) {
        if (row.nameLabel) DestroyWindow(row.nameLabel);
        if (row.valueEdit) DestroyWindow(row.valueEdit);
        if (row.currentLabel) DestroyWindow(row.currentLabel);
    }
    runtimeVariables.clear();

    Program program;
    std::string message;
    if (!parseAndCheck(program, message)) {
        setText(runtimeStage, L"먼저 올바른 코드를 작성하고 검사해 주세요.");
        publishRuntimeVariables();
        layoutControls(mainWindow);
        return;
    }
    for (const auto &statement : program.statements) {
        const Type variableType = adjustableType(statement);
        if (statement.kind != Statement::Kind::Variable || !isAdjustable(variableType))
            continue;
        RuntimeVariableRow row;
        row.name = statement.name;
        row.type = variableType;
        row.currentValue = "실행 전";
        const int baseId = 1200 + static_cast<int>(runtimeVariables.size()) * 3;
        const DWORD variableStyle = WS_CHILD | (webMode ? 0 : WS_VISIBLE);
        row.nameLabel = CreateWindowExW(0, L"STATIC", toWide(row.name).c_str(), variableStyle | SS_LEFT,
                                        0, 0, 0, 0, mainWindow,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(baseId)),
                                        GetModuleHandleW(nullptr), nullptr);
        const std::wstring typeLabel = L"초기값 · " + toWide(typeName(row.type));
        row.currentLabel = CreateWindowExW(0, L"STATIC", typeLabel.c_str(), variableStyle | SS_LEFT,
                                           0, 0, 0, 0, mainWindow,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(baseId + 1)),
                                           GetModuleHandleW(nullptr), nullptr);
        row.valueEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", toWide(defaultVariableText(statement)).c_str(),
                                        variableStyle | ES_AUTOHSCROLL, 0, 0, 0, 0, mainWindow,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(baseId + 2)),
                                        GetModuleHandleW(nullptr), nullptr);
        if (const auto found = saved.find(row.name); found != saved.end())
            setText(row.valueEdit, found->second);
        for (const auto control : {row.nameLabel, row.valueEdit, row.currentLabel})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        runtimeVariables.push_back(std::move(row));
    }
    setText(runtimeStage, runtimeVariables.empty()
                              ? L"이 파일에는 조절할 전역 숫자·문자 변수가 없습니다."
                              : L"실행 화면\n\n오른쪽에서 시작 변수 값을 조절한 뒤 실행하세요.\n실행 결과는 아래 출력 패널에 표시됩니다.");
    runtimeVariablesScroll = 0;
    layoutControls(mainWindow);
    publishRuntimeVariables();
}

void publishRuntimeVariables() {
    if (!webReady)
        return;
    std::string json = "[";
    for (std::size_t index = 0; index < runtimeVariables.size(); ++index) {
        const auto &row = runtimeVariables[index];
        if (index) json += ',';
        const std::string value = row.valueEdit ? toUtf8(windowText(row.valueEdit)) : "";
        json += "{\"name\":" + jsonQuoted(row.name) + ",\"type\":" + jsonQuoted(std::string(typeName(row.type))) +
                ",\"value\":" + jsonQuoted(value) + ",\"current\":" + jsonQuoted(row.currentValue) + "}";
    }
    json += ']';
    postWebMessage(L"variables\n" + toWide(json));
}

bool readRuntimeInitialValues(RunOptions &options, std::wstring &error) {
    refreshRuntimeVariables(true);
    for (const auto &row : runtimeVariables) {
        const std::string value = toUtf8(windowText(row.valueEdit));
        if (row.type == Type::Int) {
            std::int64_t parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                error = L"'" + toWide(row.name) + L"' 값에 정수를 입력해 주세요.";
                return false;
            }
            options.initialValues[row.name] = Value(parsed);
        } else if (row.type == Type::Float) {
            char *end{};
            const double parsed = std::strtod(value.c_str(), &end);
            if (value.empty() || end != value.c_str() + value.size() || !std::isfinite(parsed)) {
                error = L"'" + toWide(row.name) + L"' 값에 실수를 입력해 주세요.";
                return false;
            }
            options.initialValues[row.name] = Value(parsed);
        } else if (row.type == Type::Bool) {
            if (value == "true" || value == "참") options.initialValues[row.name] = Value(true);
            else if (value == "false" || value == "거짓") options.initialValues[row.name] = Value(false);
            else {
                error = L"'" + toWide(row.name) + L"' 값은 true 또는 false여야 합니다.";
                return false;
            }
        } else {
            options.initialValues[row.name] = Value(value);
        }
    }
    return true;
}

void showWorkspacePage(WorkspacePage page) {
    workspacePage = page;
    if (page == WorkspacePage::Runtime)
        refreshRuntimeVariables(true);
    else
        layoutControls(mainWindow);
}

bool saveCurrent() {
    if (currentPath.empty()) {
        const auto selected = samat::editor::chooseSaveScriptFile();
        if (!selected)
            return false;
        currentPath = *selected;
    }
    std::string error;
    if (!samat::editor::saveScriptFile(currentPath, editorSource(), error)) {
        MessageBoxW(mainWindow, toWide(error).c_str(), L"저장하지 못했습니다", MB_OK | MB_ICONERROR);
        return false;
    }
    documentDirty = false;
    updatePathLabel();
    return true;
}

void openFile() {
    const auto selected = samat::editor::chooseOpenScriptFile(currentPath);
    if (selected)
        loadPath(*selected);
}

void openHaeryeFile() {
    const auto selected = samat::editor::chooseOpenHaeryeFile(haeryePath);
    if (selected)
        loadHaeryePath(*selected);
}

bool containsLineComment(std::string_view source) {
    char quote = '\0';
    bool escaped = false;
    for (const char ch : source) {
        if (quote != '\0') {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == quote)
                quote = '\0';
        } else if (ch == '"' || ch == '\'')
            quote = ch;
        else if (ch == '#')
            return true;
    }
    return false;
}

void convertSurface() {
    if (hasActiveRun)
        return;
    const auto source = editorSource();
    Program program;
    Diagnostic diagnostic;
    if (!parseSurface(source, currentSurface, program, diagnostic)) {
        setText(output, L"현재 문법을 먼저 올바르게 작성해야 변환할 수 있습니다.\n" + toWide(diagnostic.message));
        setText(outputCaption, L"변환할 수 없음");
        return;
    }
    if (containsLineComment(source) &&
        MessageBoxW(mainWindow,
                    L"문법 변환 과정에서 주석은 유지되지 않을 수 있습니다.\n계속 변환할까요?",
                    L"주석 확인", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    const Surface destination = currentSurface == Surface::Code ? Surface::Korean : Surface::Code;
    const auto converted = destination == Surface::Code ? renderCode(program) : renderKorean(program);
    const auto convertedWide = toWide(converted);
    CHARRANGE selection{};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(editor, EM_SETSEL, 0, -1);
    SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(convertedWide.c_str()));
    currentSurface = destination;
    if (!engineProjectMode)
        documentDirty = true;
    updatePathLabel();
    SetTimer(mainWindow, highlightTimer, 1, nullptr);
    InvalidateRect(gutter, nullptr, TRUE);
    setText(output, L"문법을 " + toWide(surfaceName(currentSurface)) +
                        L"로 변환했습니다. Ctrl+Z로 되돌릴 수 있습니다.");
    setText(outputCaption, L"변환 완료");
    if (engineProjectMode)
        postWebMessage(L"engine-code\n" + convertedWide);
    const LONG newCaret = std::min<LONG>(selection.cpMin, static_cast<LONG>(convertedWide.size()));
    SendMessageW(editor, EM_SETSEL, newCaret, newCaret);
}

void newDocument() {
    if (documentDirty && !saveCurrent())
        return;
    currentPath.clear();
    currentSurface = Surface::Code;
    suppressEditorChange = true;
    SetWindowTextW(editor, L"");
    suppressEditorChange = false;
    documentDirty = false;
    updatePathLabel();
    SetTimer(mainWindow, highlightTimer, 1, nullptr);
    InvalidateRect(gutter, nullptr, TRUE);
    setText(output, L"새 Samat 문서입니다. Code Syntax로 작성하거나 訓C正音으로 전환하세요.");
    setText(outputCaption, L"새 문서");
    SetFocus(editor);
}

void toggleFindBar(bool replaceMode = false) {
    findBarVisible = true;
    findReplaceVisible = replaceMode;
    ShowWindow(findInput, SW_SHOW);
    ShowWindow(findNextButton, SW_SHOW);
    ShowWindow(findCloseButton, SW_SHOW);
    ShowWindow(replaceInput, replaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceButton, replaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceAllButton, replaceMode ? SW_SHOW : SW_HIDE);
    layoutControls(mainWindow);
    SetFocus(findInput);
    SendMessageW(findInput, EM_SETSEL, 0, -1);
}

void closeFindBar() {
    findBarVisible = false;
    ShowWindow(findInput, SW_HIDE);
    ShowWindow(replaceInput, SW_HIDE);
    ShowWindow(findNextButton, SW_HIDE);
    ShowWindow(replaceButton, SW_HIDE);
    ShowWindow(replaceAllButton, SW_HIDE);
    ShowWindow(findCloseButton, SW_HIDE);
    layoutControls(mainWindow);
    SetFocus(editor);
}

bool equalAt(std::wstring_view source, std::size_t offset, std::wstring_view needle) {
    return offset <= source.size() && needle.size() <= source.size() - offset &&
           CompareStringOrdinal(source.data() + offset, static_cast<int>(needle.size()), needle.data(),
                                static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL;
}

void findNext(bool backwards = false) {
    const std::wstring needle = windowText(findInput);
    const std::wstring source = windowText(editor);
    if (needle.empty() || needle.size() > source.size()) {
        MessageBeep(MB_ICONINFORMATION);
        return;
    }
    LONG selectionStart{}, selectionEnd{};
    SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                 reinterpret_cast<LPARAM>(&selectionEnd));
    const std::size_t initial = backwards ? (selectionStart > 0 ? static_cast<std::size_t>(selectionStart - 1)
                                                               : source.size() - needle.size())
                                          : static_cast<std::size_t>(selectionEnd);
    for (std::size_t step = 0; step < source.size(); ++step) {
        const std::size_t offset = backwards ? (initial + source.size() - step) % source.size()
                                             : (initial + step) % source.size();
        if (!equalAt(source, offset, needle))
            continue;
        SendMessageW(editor, EM_SETSEL, static_cast<WPARAM>(offset), static_cast<LPARAM>(offset + needle.size()));
        SetFocus(editor);
        return;
    }
    MessageBeep(MB_ICONINFORMATION);
}

void replaceCurrent() {
    const std::wstring needle = windowText(findInput);
    if (needle.empty())
        return;
    LONG start{}, end{};
    SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    const auto source = windowText(editor);
    if (start < 0 || end < start || !equalAt(source, static_cast<std::size_t>(start), needle)) {
        findNext();
        return;
    }
    const std::wstring replacement = windowText(replaceInput);
    SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
    findNext();
}

void replaceAll() {
    const std::wstring needle = windowText(findInput);
    if (needle.empty())
        return;
    const auto source = windowText(editor);
    const std::wstring replacement = windowText(replaceInput);
    std::wstring result;
    result.reserve(source.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < source.size();) {
        if (equalAt(source, i, needle)) {
            result += replacement;
            i += needle.size();
            ++count;
        } else
            result.push_back(source[i++]);
    }
    if (count == 0) {
        MessageBeep(MB_ICONINFORMATION);
        return;
    }
    SendMessageW(editor, EM_SETSEL, 0, -1);
    SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(result.c_str()));
    setText(output, L"" + std::to_wstring(count) + L"곳을 바꿨습니다. Ctrl+Z로 되돌릴 수 있습니다.");
    setText(outputCaption, L"바꾸기 완료");
}

void runSource() {
    Program program;
    std::string message;
    if (!parseAndCheck(program, message)) {
        setText(output, toWide(message));
        setText(outputCaption, L"오류");
        publishRuntimeOutput();
        return;
    }
    const auto source = editorSource();
    if (source.find("readLine(") != std::string::npos || source.find("readLine ()") != std::string::npos) {
        setText(output, L"이 예제는 콘솔 입력을 사용합니다. 현재 Studio 실행은 readLine 입력을 받지 않으므로\n저장한 뒤 명령줄에서 실행해 주세요.");
        setText(outputCaption, L"콘솔 입력 예제");
        publishRuntimeOutput();
        return;
    }
    if (hasActiveRun)
        return;

    RunOptions options;
    options.instructionBudget = 250'000;
    options.outputByteLimit = 96 * 1024;
    std::wstring valueError;
    if (!readRuntimeInitialValues(options, valueError)) {
        setText(output, valueError);
        setText(outputCaption, L"변수 값 확인");
        publishRuntimeOutput();
        return;
    }
    const auto mainFunction = std::find_if(program.statements.begin(), program.statements.end(), [](const Statement &item) {
        return item.kind == Statement::Kind::Function && item.name == "main";
    });
    if (mainFunction != program.statements.end())
        options.entryFunction = "main";
    if (!interpreter.start(std::move(program), std::move(options))) {
        setText(output, L"이전 실행이 아직 종료되지 않았습니다.");
        setText(outputCaption, L"실행 중");
        publishRuntimeOutput();
        return;
    }
    hasActiveRun = true;
    showWorkspacePage(WorkspacePage::Runtime);
    SetWindowTextW(runButton, L"중지");
    EnableWindow(checkButton, FALSE);
    setText(output, L"실행 중…");
    setText(outputCaption, L"실행 중");
    publishRuntimeOutput();
    SetTimer(mainWindow, runTimer, 40, nullptr);
}

void pollRun() {
    const auto result = interpreter.snapshot();
    if (!result.completed)
        return;
    KillTimer(mainWindow, runTimer);
    interpreter.joinCompleted();
    hasActiveRun = false;
    SetWindowTextW(runButton, L"실행");
    EnableWindow(checkButton, TRUE);
    std::string text;
    for (const auto &line : result.output) {
        text += line;
        text += '\n';
    }
    if (!result.error.empty())
        text += "오류: " + result.error + '\n';
    else if (!result.cancelled)
        text += "완료 · 실행 명령 " + std::to_string(result.instructionsExecuted) + "개";
    else
        text += "실행을 중지했습니다.";
    if (text.empty())
        text = "실행을 마쳤습니다.";
    if (result.error.empty() && !result.cancelled && !result.returnValue.isNull())
        text += "\n반환값: " + result.returnValue.toString();
    for (const auto &[name, value] : result.variables) {
        text += "\n" + name + " = " + value;
        const auto row = std::find_if(runtimeVariables.begin(), runtimeVariables.end(),
                                      [&](const RuntimeVariableRow &item) { return item.name == name; });
        if (row != runtimeVariables.end()) {
            row->currentValue = value;
            setText(row->currentLabel, L"현재값 · " + toWide(value));
        }
    }
    setText(output, toWide(text));
    setText(outputCaption, result.error.empty() ? (result.cancelled ? L"중지됨" : L"완료") : L"오류");
    publishRuntimeVariables();
    publishRuntimeOutput();
}

COLORREF syntaxColor(samat::editor::SyntaxKind kind) {
    switch (kind) {
    case samat::editor::SyntaxKind::Keyword:
        return RGB(211, 184, 255);
    case samat::editor::SyntaxKind::Builtin:
        return RGB(132, 218, 198);
    case samat::editor::SyntaxKind::Number:
        return RGB(244, 199, 137);
    case samat::editor::SyntaxKind::String:
        return RGB(174, 218, 158);
    case samat::editor::SyntaxKind::Comment:
        return RGB(151, 145, 174);
    case samat::editor::SyntaxKind::Operator:
        return RGB(203, 197, 218);
    case samat::editor::SyntaxKind::Identifier:
        return foregroundColor;
    }
    return foregroundColor;
}

COLORREF sceneColor(jm::HaeryeColor color) {
    return RGB(static_cast<BYTE>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F),
               static_cast<BYTE>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F),
               static_cast<BYTE>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F));
}

void drawRuntimeStage(const DRAWITEMSTRUCT *item) {
    HDC dc = item->hDC;
    const RECT bounds = item->rcItem;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    RECT canvas{bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1};
    const COLORREF background = runtimeSceneLoaded ? sceneColor(runtimeScene.backgroundColor) : stageColor;
    HBRUSH back = CreateSolidBrush(background);
    FillRect(dc, &canvas, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    if (!runtimeSceneLoaded) {
        SetTextColor(dc, RGB(102, 92, 133));
        RECT prompt = canvas;
        DrawTextW(dc, L"해례(.hy)를 열면 이곳에 장면이 표시됩니다.\nSamat(.st) 코드는 런타임에서 함께 실행됩니다.",
                  -1, &prompt, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        return;
    }

    const int inset = std::max(26, std::min(width, height) / 15);
    RECT playfield{canvas.left + inset, canvas.top + inset, canvas.right - inset, canvas.bottom - inset};
    HPEN gridPen = CreatePen(PS_DOT, 1, runtimeScene.backgroundColor.r < 0.5F ? RGB(67, 64, 82) : RGB(218, 214, 228));
    const auto oldPen = SelectObject(dc, gridPen);
    MoveToEx(dc, (playfield.left + playfield.right) / 2, playfield.top, nullptr);
    LineTo(dc, (playfield.left + playfield.right) / 2, playfield.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    const double worldWidth = 16.0;
    const double worldHeight = worldWidth * (playfield.bottom - playfield.top) /
                               static_cast<double>(std::max(1L, playfield.right - playfield.left));
    for (const auto &object : runtimeScene.objects) {
        if (!object.position || !object.size || !object.shape)
            continue;
        const double x = (*object.position)[0];
        const double y = (*object.position)[1];
        const double objectWidth = std::abs((*object.size)[0]);
        const double objectHeight = std::abs((*object.size)[1]);
        const int centerX = playfield.left + static_cast<int>((x + worldWidth / 2.0) / worldWidth *
                                                             (playfield.right - playfield.left));
        const int centerY = playfield.top + static_cast<int>((worldHeight / 2.0 - y) / worldHeight *
                                                             (playfield.bottom - playfield.top));
        const int halfWidth = std::max(3, static_cast<int>(objectWidth / worldWidth *
                                                          (playfield.right - playfield.left) / 2.0));
        const int halfHeight = std::max(3, static_cast<int>(objectHeight / worldHeight *
                                                           (playfield.bottom - playfield.top) / 2.0));
        const COLORREF fillColor = object.color ? sceneColor(*object.color) : RGB(130, 116, 177);
        HBRUSH objectBrush = CreateSolidBrush(fillColor);
        HPEN outline = CreatePen(PS_SOLID, 1, fillColor);
        const auto previousBrush = SelectObject(dc, objectBrush);
        const auto previousPen = SelectObject(dc, outline);
        if (*object.shape == jm::HaeryeShape::Circle)
            Ellipse(dc, centerX - halfWidth, centerY - halfHeight, centerX + halfWidth, centerY + halfHeight);
        else
            RoundRect(dc, centerX - halfWidth, centerY - halfHeight, centerX + halfWidth, centerY + halfHeight, 5, 5);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(objectBrush);
        DeleteObject(outline);
        SetTextColor(dc, runtimeScene.backgroundColor.r < 0.5F ? RGB(235, 231, 245) : RGB(55, 49, 74));
        RECT label{centerX - 70, centerY + halfHeight + 4, centerX + 70, centerY + halfHeight + 24};
        DrawTextW(dc, toWide(object.name).c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SetTextColor(dc, runtimeScene.backgroundColor.r < 0.5F ? RGB(202, 194, 222) : RGB(94, 83, 119));
    RECT title{canvas.left + 14, canvas.top + 10, canvas.right - 14, canvas.top + 32};
    const std::wstring titleText = L"HAERYE  /  " + toWide(runtimeScene.name);
    DrawTextW(dc, titleText.c_str(), -1, &title, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

std::vector<LONG> utf8ToRichEditPositions(std::string_view source) {
    std::vector<LONG> positions(source.size() + 1);
    std::size_t byte = 0;
    LONG wide = 0;
    while (byte < source.size()) {
        const auto lead = static_cast<unsigned char>(source[byte]);
        const std::size_t length = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 :
                                                 (lead & 0xf0) == 0xe0 ? 3 :
                                                 (lead & 0xf8) == 0xf0 ? 4 : 1;
        const std::size_t boundedLength = std::min(length, source.size() - byte);
        for (std::size_t offset = 0; offset < boundedLength; ++offset)
            positions[byte + offset] = wide;
        byte += boundedLength;
        wide += length == 4 ? 2 : 1;
    }
    positions[source.size()] = wide;
    return positions;
}

void applySyntaxColors() {
    if (!editor || !richEditModule)
        return;
    const std::string source = editorSource();
    CHARRANGE oldSelection{};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&oldSelection));
    const auto positions = utf8ToRichEditPositions(source);
    CHARRANGE allText{0, positions[source.size()]};
    suppressEditorChange = true;
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allText));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = foregroundColor;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    for (std::size_t offset = 0; offset < source.size();) {
        const auto token = samat::editor::syntaxTokenAt(source, offset);
        if (!token || token->range.end <= offset) {
            ++offset;
            continue;
        }
        if (token->kind != samat::editor::SyntaxKind::Identifier) {
            CHARRANGE range{positions[token->range.begin], positions[token->range.end]};
            SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
            format.crTextColor = syntaxColor(token->kind);
            SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        }
        offset = token->range.end;
    }
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&oldSelection));
    suppressEditorChange = false;
}

void updateCursorLabel() {
    if (!editor || !cursorLabel)
        return;
    LONG caret{};
    SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&caret), 0);
    const LONG line = static_cast<LONG>(SendMessageW(editor, EM_LINEFROMCHAR, caret, 0));
    const LONG lineStart = static_cast<LONG>(SendMessageW(editor, EM_LINEINDEX, line, 0));
    setText(cursorLabel, L"Ln " + std::to_wstring(line + 1) + L", Col " +
                              std::to_wstring(caret - lineStart + 1));
}

LRESULT CALLBACK gutterSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, panelBrush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, mutedColor);
        const auto oldFont = SelectObject(dc, editorFont);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        const int lineHeight = std::max(1, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));
        const int first = static_cast<int>(SendMessageW(editor, EM_GETFIRSTVISIBLELINE, 0, 0));
        const int count = static_cast<int>(SendMessageW(editor, EM_GETLINECOUNT, 0, 0));
        for (int line = first; line < count; ++line) {
            RECT row{0, (line - first) * lineHeight, client.right - 8, (line - first + 1) * lineHeight};
            if (row.top >= client.bottom)
                break;
            const auto number = std::to_wstring(line + 1);
            DrawTextW(dc, number.c_str(), -1, &row, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, oldFont);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, gutterSubclassProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

void insertText(std::wstring_view text) {
    SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(std::wstring(text).c_str()));
}

LRESULT CALLBACK editorSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR subclassId, DWORD_PTR referenceData) {
    if (message == WM_GETDLGCODE)
        return DefSubclassProc(window, message, wParam, lParam) | DLGC_WANTTAB;
    if (message == WM_KEYDOWN) {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (control) {
            switch (wParam) {
            case 'N':
                newDocument();
                return 0;
            case 'O':
                openFile();
                return 0;
            case 'S':
                saveCurrent();
                return 0;
            case 'F':
                toggleFindBar(false);
                return 0;
            case 'H':
                toggleFindBar(true);
                return 0;
            case 'T':
                if (shift)
                    convertSurface();
                return 0;
            case VK_RETURN:
                runSource();
                return 0;
            }
        }
        if (wParam == VK_F5) {
            runSource();
            return 0;
        }
        if (wParam == VK_F3) {
            findNext(shift);
            return 0;
        }
    }
    if (message == WM_CHAR) {
        if (wParam == '\t') {
            insertText(L"    ");
            return 0;
        }
        if (wParam == '\r') {
            LONG caret{}, selectionEnd{};
            SendMessageW(window, EM_GETSEL, reinterpret_cast<WPARAM>(&caret),
                         reinterpret_cast<LPARAM>(&selectionEnd));
            const auto source = windowText(window);
            const LONG line = static_cast<LONG>(SendMessageW(window, EM_LINEFROMCHAR, caret, 0));
            const LONG lineStart = static_cast<LONG>(SendMessageW(window, EM_LINEINDEX, line, 0));
            std::wstring indent;
            for (LONG index = lineStart; index < caret && index < static_cast<LONG>(source.size()) &&
                                        (source[static_cast<std::size_t>(index)] == L' ' ||
                                         source[static_cast<std::size_t>(index)] == L'\t');
                 ++index)
                indent.push_back(source[static_cast<std::size_t>(index)]);
            LONG bodyEnd = std::min<LONG>(caret, static_cast<LONG>(source.size()));
            while (bodyEnd > lineStart && (source[static_cast<std::size_t>(bodyEnd - 1)] == L' ' ||
                                           source[static_cast<std::size_t>(bodyEnd - 1)] == L'\t'))
                --bodyEnd;
            if (bodyEnd > lineStart && source[static_cast<std::size_t>(bodyEnd - 1)] == L':')
                indent += L"    ";
            const auto handled = DefSubclassProc(window, message, wParam, lParam);
            if (!indent.empty())
                insertText(indent);
            return handled;
        }
        if (wParam == '(' || wParam == '[' || wParam == '{' || wParam == '"' || wParam == '\'') {
            LONG start{}, end{};
            SendMessageW(window, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            if (start == end) {
                const wchar_t closing = wParam == '(' ? L')' : wParam == '[' ? L']' :
                                        wParam == '{' ? L'}' : static_cast<wchar_t>(wParam);
                const wchar_t pair[]{static_cast<wchar_t>(wParam), closing, L'\0'};
                insertText(pair);
                SendMessageW(window, EM_SETSEL, start + 1, start + 1);
                return 0;
            }
        }
        if (wParam == ')' || wParam == ']' || wParam == '}') {
            LONG start{}, end{};
            SendMessageW(window, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            const auto source = windowText(window);
            if (start == end && start < static_cast<LONG>(source.size()) &&
                source[static_cast<std::size_t>(start)] == static_cast<wchar_t>(wParam)) {
                SendMessageW(window, EM_SETSEL, start + 1, start + 1);
                return 0;
            }
        }
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, editorSubclassProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

void createControls(HWND window) {
    const auto create = [window](DWORD exStyle, const wchar_t *klass, const wchar_t *text, DWORD style,
                                 int id) -> HWND {
        return CreateWindowExW(exStyle, klass, text, style, 0, 0, 0, 0, window,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    };
    titleLabel = create(0, L"STATIC", L"Samat Studio", WS_CHILD | WS_VISIBLE | SS_LEFT, 0);
    pathLabel = create(0, L"STATIC", L"예제를 선택하세요", WS_CHILD | WS_VISIBLE | SS_LEFT, 0);
    categoryCaption = create(0, L"STATIC", L"범주", WS_CHILD | WS_VISIBLE | SS_LEFT, 0);
    examplesCaption = create(0, L"STATIC", L"예제", WS_CHILD | WS_VISIBLE | SS_LEFT, 0);
    outputCaption = create(0, L"STATIC", L"출력", WS_CHILD | WS_VISIBLE | SS_LEFT, idOutputLabel);
    categoryList = create(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                          idCategories);
    exampleList = create(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                             LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                         idExamples);
    editor = create(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                    idEditor);
    if (richEditModule) {
        DestroyWindow(editor);
        editor = create(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                            WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN |
                            ES_NOHIDESEL,
                        idEditor);
    }
    output = create(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                    idOutput);
    newButton = create(0, L"BUTTON", L"＋ 새 문서", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idNew);
    openButton = create(0, L"BUTTON", L"▣ Samat", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idOpen);
    openHaeryeButton = create(0, L"BUTTON", L"◇ 해례", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idOpenHaerye);
    saveButton = create(0, L"BUTTON", L"↓ 저장", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idSave);
    checkButton = create(0, L"BUTTON", L"✓ 검사", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idCheck);
    runButton = create(0, L"BUTTON", L"▶ 실행", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idRun);
    convertButton = create(0, L"BUTTON", L"⇄ 문법 전환", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idConvert);
    findButton = create(0, L"BUTTON", L"⌕ 찾기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idFindToggle);
    gutter = create(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, idGutter);
    findInput = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, idFindInput);
    replaceInput = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, idReplaceInput);
    findNextButton = create(0, L"BUTTON", L"다음", WS_CHILD | BS_OWNERDRAW, idFindNext);
    replaceButton = create(0, L"BUTTON", L"바꾸기", WS_CHILD | BS_OWNERDRAW, idReplace);
    replaceAllButton = create(0, L"BUTTON", L"모두 바꾸기", WS_CHILD | BS_OWNERDRAW, idReplaceAll);
    findCloseButton = create(0, L"BUTTON", L"닫기", WS_CHILD | BS_OWNERDRAW, idFindClose);
    cursorLabel = create(0, L"STATIC", L"Ln 1, Col 1", WS_CHILD | WS_VISIBLE | SS_RIGHT, idCursorLabel);
    sidebarToggleButton = create(0, L"BUTTON", L"탐색기 숨기기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 idSidebarToggle);
    examplesFilter = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           idExamplesFilter);
    codePageButton = create(0, L"BUTTON", L"⌘ 코드 편집", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idCodePage);
    runtimePageButton = create(0, L"BUTTON", L"◫ 런타임", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idRuntimePage);
    runtimeStage = create(0, L"STATIC", L"", WS_CHILD | SS_OWNERDRAW,
                         1100);
    runtimeInspectorCaption = create(0, L"STATIC", L"변수 조절", WS_CHILD | SS_LEFT, 1101);
    refreshVariablesButton = create(0, L"BUTTON", L"변수 다시 읽기", WS_CHILD | BS_OWNERDRAW,
                                    idRefreshVariables);
    SetWindowSubclass(editor, editorSubclassProc, 1, 0);
    SetWindowSubclass(gutter, gutterSubclassProc, 1, 0);
    SendMessageW(findInput, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"찾을 문자열"));
    SendMessageW(replaceInput, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"바꿀 문자열"));
    SendMessageW(examplesFilter, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"예제 검색…"));
    SendMessageW(editor, EM_SETBKGNDCOLOR, 0, raisedColor);
    SendMessageW(editor, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
    SendMessageW(editor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL);
    CHARFORMAT2W defaultFormat{};
    defaultFormat.cbSize = sizeof(defaultFormat);
    defaultFormat.dwMask = CFM_COLOR;
    defaultFormat.crTextColor = foregroundColor;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&defaultFormat));

    uiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    editorFont = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
                             L"Cascadia Mono");
    cardTitleFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                L"Segoe UI");
    cardDetailFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 L"Segoe UI");
    for (const auto control : {titleLabel, pathLabel, categoryCaption, examplesCaption, outputCaption,
                               categoryList, exampleList, newButton, openButton, openHaeryeButton, saveButton, checkButton,
                               runButton, convertButton, findButton, findInput, replaceInput, findNextButton,
                               replaceButton, replaceAllButton, findCloseButton, cursorLabel,
                               sidebarToggleButton, examplesFilter, codePageButton, runtimePageButton,
                               runtimeInspectorCaption, refreshVariablesButton, runtimeStage})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    SendMessageW(editor, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);
    SendMessageW(output, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);
    SendMessageW(gutter, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);
    ShowWindow(findInput, SW_HIDE);
    ShowWindow(replaceInput, SW_HIDE);
    ShowWindow(findNextButton, SW_HIDE);
    ShowWindow(replaceButton, SW_HIDE);
    ShowWindow(replaceAllButton, SW_HIDE);
    ShowWindow(findCloseButton, SW_HIDE);

    SendMessageW(categoryList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"모든 예제"));
    for (const auto &category : categories)
        SendMessageW(categoryList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(category.name));
    SendMessageW(categoryList, LB_SETCURSEL, 0, 0);
    refreshExampleList();
}

void layoutControls(HWND window) {
    if (webMode) {
        if (webController) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            webController->put_Bounds(bounds);
        }
        return;
    }
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = 16;
    const bool compact = width < 1060;
    const int topHeight = compact ? 106 : 126;
    const int outputHeight = std::clamp(outputPanelHeight, 100, std::max(100, height / 2));
    const bool runtime = workspacePage == WorkspacePage::Runtime;
    const int sidebarWidth = !runtime && sidebarVisible
                                 ? std::clamp(sidebarWidthSetting, 190, std::max(190, width / 3))
                                 : 0;
    const int gap = sidebarWidth > 0 ? 14 : 0;
    const int left = margin + sidebarWidth + gap;
    const int contentWidth = std::max(220, width - left - margin);
    const int editorTop = findBarVisible && !runtime ? 142 : topHeight + 8;
    const int outputTop = std::max(editorTop + 170, height - outputHeight - margin);
    const int editorHeight = std::max(120, outputTop - editorTop - 10);

    MoveWindow(titleLabel, margin, 13, 185, 29, TRUE);
    const int pageButtonWidth = compact ? 96 : 112;
    MoveWindow(codePageButton, std::max(210, width - margin - 2 * pageButtonWidth - 8), 12,
               pageButtonWidth, 32, TRUE);
    MoveWindow(runtimePageButton, std::max(210, width - margin - pageButtonWidth), 12,
               pageButtonWidth, 32, TRUE);
    MoveWindow(pathLabel, margin + 190, 17,
               std::max(0, width - 2 * margin - 190 - 2 * pageButtonWidth - 20), 24, TRUE);
    const int buttonWidth = compact ? 72 : 78;
    const int buttonGap = 6;
    int buttonX = margin;
    const int toolbarY = 49;
    MoveWindow(sidebarToggleButton, buttonX, toolbarY, 124, 34, TRUE);
    buttonX += 130;
    MoveWindow(newButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(openButton, buttonX, toolbarY, 88, 34, TRUE);
    buttonX += 88 + buttonGap;
    MoveWindow(openHaeryeButton, buttonX, toolbarY, 82, 34, TRUE);
    buttonX += 82 + buttonGap;
    MoveWindow(saveButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(checkButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(runButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(convertButton, buttonX, toolbarY, 112, 34, TRUE);
    buttonX += 112 + buttonGap;
    MoveWindow(findButton, buttonX, toolbarY, buttonWidth, 34, TRUE);

    const int findY = 91;
    if (findBarVisible) {
        int findX = left;
        const int findWidth = findReplaceVisible ? std::clamp(contentWidth / 4, 190, 260) : 300;
        MoveWindow(findInput, findX, findY, findWidth, 34, TRUE);
        findX += findWidth + 7;
        MoveWindow(findNextButton, findX, findY, 66, 34, TRUE);
        findX += 73;
        if (findReplaceVisible) {
            const int replaceWidth = std::clamp(contentWidth / 4, 190, 260);
            MoveWindow(replaceInput, findX, findY, replaceWidth, 34, TRUE);
            findX += replaceWidth + 7;
            MoveWindow(replaceButton, findX, findY, 70, 34, TRUE);
            findX += 77;
            MoveWindow(replaceAllButton, findX, findY, 96, 34, TRUE);
            findX += 103;
        }
        MoveWindow(findCloseButton, findX, findY, 64, 34, TRUE);
    }

    const int sidebarY = editorTop;
    const bool showCodeSidebar = !runtime && sidebarVisible;
    for (const auto control : {categoryCaption, categoryList, examplesCaption, examplesFilter, exampleList})
        ShowWindow(control, showCodeSidebar ? SW_SHOW : SW_HIDE);
    ShowWindow(sidebarToggleButton, runtime ? SW_HIDE : SW_SHOW);
    ShowWindow(findButton, runtime ? SW_HIDE : SW_SHOW);
    ShowWindow(convertButton, runtime ? SW_HIDE : SW_SHOW);
    ShowWindow(codePageButton, SW_SHOW);
    ShowWindow(runtimePageButton, SW_SHOW);
    if (showCodeSidebar) {
        const int categoryHeight = std::clamp((outputTop - sidebarY) / 5, 82, 126);
        MoveWindow(categoryCaption, margin, sidebarY, sidebarWidth, 23, TRUE);
        MoveWindow(categoryList, margin, sidebarY + 26, sidebarWidth, categoryHeight, TRUE);
        MoveWindow(examplesCaption, margin, sidebarY + 34 + categoryHeight, sidebarWidth, 23, TRUE);
        MoveWindow(examplesFilter, margin, sidebarY + 60 + categoryHeight, sidebarWidth, 32, TRUE);
        MoveWindow(exampleList, margin, sidebarY + 99 + categoryHeight,
                   sidebarWidth, std::max(60, outputTop - sidebarY - categoryHeight - 110), TRUE);
    }

    const int gutterWidth = 52;
    ShowWindow(editor, runtime ? SW_HIDE : SW_SHOW);
    ShowWindow(gutter, runtime ? SW_HIDE : SW_SHOW);
    ShowWindow(findInput, (!runtime && findBarVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(findNextButton, (!runtime && findBarVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(findCloseButton, (!runtime && findBarVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceInput, (!runtime && findBarVisible && findReplaceVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceButton, (!runtime && findBarVisible && findReplaceVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceAllButton, (!runtime && findBarVisible && findReplaceVisible) ? SW_SHOW : SW_HIDE);
    ShowWindow(runtimeStage, runtime ? SW_SHOW : SW_HIDE);
    ShowWindow(runtimeInspectorCaption, runtime ? SW_SHOW : SW_HIDE);
    ShowWindow(refreshVariablesButton, runtime ? SW_SHOW : SW_HIDE);
    if (!runtime) {
        MoveWindow(gutter, left, editorTop, gutterWidth, editorHeight, TRUE);
        MoveWindow(editor, left + gutterWidth, editorTop, std::max(120, contentWidth - gutterWidth), editorHeight,
                   TRUE);
    } else {
        const int inspectorWidth = std::clamp(width / 3, 330, 480);
        const int inspectorLeft = width - margin - inspectorWidth;
        const int stageWidth = std::max(240, inspectorLeft - left - 14);
        MoveWindow(runtimeStage, left, editorTop, stageWidth, editorHeight, TRUE);
        MoveWindow(runtimeInspectorCaption, inspectorLeft, editorTop, inspectorWidth - 126, 28, TRUE);
        MoveWindow(refreshVariablesButton, inspectorLeft + inspectorWidth - 120, editorTop - 2, 120, 32, TRUE);
        const int rowHeight = 78;
        const int rowsTop = editorTop + 42;
        const int visibleRows = std::max(0, (editorHeight - 46) / rowHeight);
        runtimeVariablesScroll = std::clamp(runtimeVariablesScroll, 0,
            std::max(0, static_cast<int>(runtimeVariables.size()) - visibleRows));
        for (std::size_t index = 0; index < runtimeVariables.size(); ++index) {
            auto &row = runtimeVariables[index];
            const int visibleIndex = static_cast<int>(index) - runtimeVariablesScroll;
            const bool visible = visibleIndex >= 0 && visibleIndex < visibleRows;
            for (const auto control : {row.nameLabel, row.valueEdit, row.currentLabel})
                ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
            if (!visible) continue;
            const int y = rowsTop + visibleIndex * rowHeight;
            MoveWindow(row.nameLabel, inspectorLeft, y, inspectorWidth, 22, TRUE);
            MoveWindow(row.valueEdit, inspectorLeft, y + 24, inspectorWidth, 30, TRUE);
            MoveWindow(row.currentLabel, inspectorLeft, y + 56, inspectorWidth, 20, TRUE);
        }
    }
    MoveWindow(outputCaption, runtime ? margin : left, outputTop + 5,
               std::max(100, (runtime ? width - 2 * margin : contentWidth) - 150), 24, TRUE);
    MoveWindow(cursorLabel, (runtime ? width - margin : left + contentWidth) - 145, outputTop + 5, 145, 24, TRUE);
    MoveWindow(output, runtime ? margin : left, outputTop + 34,
               runtime ? std::max(220, width - 2 * margin) : contentWidth,
               std::max(55, height - outputTop - margin - 34), TRUE);
    InvalidateRect(codePageButton, nullptr, TRUE);
    InvalidateRect(runtimePageButton, nullptr, TRUE);
    InvalidateRect(window, nullptr, TRUE);
}

LRESULT drawButton(const DRAWITEMSTRUCT *item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool primary = item->CtlID == idRun;
    const bool selectedPage = (item->CtlID == idCodePage && workspacePage == WorkspacePage::Code) ||
                              (item->CtlID == idRuntimePage && workspacePage == WorkspacePage::Runtime);
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = (primary || selectedPage) ? (disabled ? RGB(126, 110, 160) : accentColor)
                                  : (disabled ? RGB(42, 39, 56) : raisedColor);
    const COLORREF ink = (primary || selectedPage) ? RGB(43, 36, 63) : foregroundColor;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? accentColor : borderColor);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SetBkMode(dc, TRANSPARENT);
    const std::wstring label = windowText(item->hwndItem);
    const auto separator = label.find(L' ');
    RECT content = rect;
    content.left += 5;
    content.right -= 4;
    if (separator != std::wstring::npos) {
        const std::wstring icon = label.substr(0, separator);
        RECT iconRect = content;
        iconRect.right = iconRect.left + 24;
        SetTextColor(dc, primary || selectedPage ? ink : accentColor);
        DrawTextW(dc, icon.c_str(), -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content.left += 23;
        SetTextColor(dc, ink);
        const std::wstring caption = label.substr(separator + 1);
        DrawTextW(dc, caption.c_str(), -1, &content, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        SetTextColor(dc, ink);
        DrawTextW(dc, label.c_str(), -1, &content, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    return TRUE;
}

void drawListItem(const DRAWITEMSTRUCT *item) {
    if (item->itemID == static_cast<UINT>(-1))
        return;
    const bool isCategory = item->CtlID == idCategories;
    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    RECT rect = item->rcItem;
    const COLORREF fill = selected ? selectedColor : panelColor;
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(item->hDC, &rect, brush);
    DeleteObject(brush);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, selected ? accentColor : foregroundColor);
    RECT textRect = rect;
    textRect.left += 16;
    textRect.right -= 12;
    textRect.top += isCategory ? 8 : 10;
    wchar_t text[192]{};
    SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(text));
    const auto titleFont = SelectObject(item->hDC, isCategory ? uiFont : cardTitleFont);
    DrawTextW(item->hDC, text, -1, &textRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!isCategory && item->itemID < visibleExamples.size()) {
        SetTextColor(item->hDC, mutedColor);
        SelectObject(item->hDC, cardDetailFont);
        textRect.top += 34;
        DrawTextW(item->hDC, visibleExamples[item->itemID]->summary, -1, &textRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(item->hDC, titleFont);
    RECT edge = item->rcItem;
    InflateRect(&edge, -1, -1);
    HBRUSH edgeBrush = CreateSolidBrush(borderColor);
    FrameRect(item->hDC, &edge, edgeBrush);
    DeleteObject(edgeBrush);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        mainWindow = window;
        if (!richEditModule)
            richEditModule = LoadLibraryW(L"Msftedit.dll");
        backgroundBrush = CreateSolidBrush(backgroundColor);
        panelBrush = CreateSolidBrush(panelColor);
        raisedBrush = CreateSolidBrush(raisedColor);
    stageBrush = CreateSolidBrush(stageColor);
        createControls(window);
        EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL {
            ShowWindow(child, SW_HIDE);
            return TRUE;
        }, 0);
        examplesPath = discoverExamplesPath();
        if (!visibleExamples.empty())
            loadPath(examplesPath / visibleExamples.front()->file);
        initializeWebView(window);
        return 0;
    case WM_ERASEBKGND: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, backgroundBrush);
        const int width = client.right;
        const int height = client.bottom;
        const int margin = 16;
        const int topHeight = width < 1060 ? 106 : 126;
        const int outputHeight = std::clamp(outputPanelHeight, 100, std::max(100, height / 2));
        const int editorTop = findBarVisible && workspacePage == WorkspacePage::Code ? 142 : topHeight + 8;
        const int outputTop = std::max(editorTop + 170, height - outputHeight - margin);
        RECT workArea{0, topHeight, width, outputTop};
        FillRect(dc, &workArea, panelBrush);
        RECT footer{0, outputTop, width, height};
        FillRect(dc, &footer, backgroundBrush);
        return 1;
    }
    case WM_SIZE:
        layoutControls(window);
        return 0;
    case webMessageDispatch: {
        std::unique_ptr<std::wstring> message(reinterpret_cast<std::wstring *>(lParam));
        if (message)
            handleWebMessage(*message);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lParam);
        limits->ptMinTrackSize.x = 850;
        limits->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            RECT client{};
            GetClientRect(window, &client);
            const int dividerY = std::max(94, static_cast<int>(client.bottom - outputPanelHeight - 16));
            if (point.y >= dividerY - 5 && point.y <= dividerY + 5) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            const int dividerX = 16 + sidebarWidthSetting + 14;
            if (sidebarVisible && point.x >= dividerX - 5 && point.x <= dividerX + 5 && point.y > 100) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT client{};
        GetClientRect(window, &client);
        const int dividerY = std::max(94, static_cast<int>(client.bottom - outputPanelHeight - 16));
        const int dividerX = 16 + sidebarWidthSetting + 14;
        if (std::abs(y - dividerY) <= 5 && x > 16 + sidebarWidthSetting) {
            draggingOutputSplitter = true;
            SetCapture(window);
            return 0;
        }
        if (workspacePage == WorkspacePage::Code && sidebarVisible &&
            std::abs(x - dividerX) <= 5 && y > 100) {
            draggingSidebarSplitter = true;
            SetCapture(window);
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL:
        if (workspacePage == WorkspacePage::Runtime && !runtimeVariables.empty()) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            runtimeVariablesScroll = std::max(0, runtimeVariablesScroll + (delta < 0 ? 1 : -1));
            layoutControls(window);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (draggingSidebarSplitter && (wParam & MK_LBUTTON)) {
            RECT client{};
            GetClientRect(window, &client);
            sidebarWidthSetting = std::clamp(GET_X_LPARAM(lParam) - 30, 190,
                                             std::max(190, static_cast<int>(client.right / 3)));
            layoutControls(window);
            return 0;
        }
        if (draggingOutputSplitter && (wParam & MK_LBUTTON)) {
            RECT client{};
            GetClientRect(window, &client);
            outputPanelHeight = std::clamp(static_cast<int>(client.bottom - GET_Y_LPARAM(lParam) - 16), 100,
                                           std::max(100, static_cast<int>(client.bottom / 2)));
            layoutControls(window);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (draggingSidebarSplitter) {
            draggingSidebarSplitter = false;
            ReleaseCapture();
            return 0;
        }
        if (draggingOutputSplitter) {
            draggingOutputSplitter = false;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (control == runtimeStage) {
            SetTextColor(dc, RGB(83, 72, 111));
            SetBkColor(dc, stageColor);
            return reinterpret_cast<LRESULT>(stageBrush);
        }
        if (control == runtimeInspectorCaption ||
            std::any_of(runtimeVariables.begin(), runtimeVariables.end(), [control](const RuntimeVariableRow &row) {
                return row.nameLabel == control || row.currentLabel == control;
            })) {
            SetTextColor(dc, control == pathLabel ? mutedColor : foregroundColor);
            SetBkColor(dc, panelColor);
            return reinterpret_cast<LRESULT>(panelBrush);
        }
        SetTextColor(dc, control == pathLabel ? mutedColor : foregroundColor);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool runtimeInput = std::any_of(runtimeVariables.begin(), runtimeVariables.end(),
            [control](const RuntimeVariableRow &row) { return row.valueEdit == control; });
        SetTextColor(dc, runtimeInput ? RGB(45, 43, 55) : foregroundColor);
        SetBkColor(dc, runtimeInput ? stageColor : raisedColor);
        return reinterpret_cast<LRESULT>(runtimeInput ? stageBrush : raisedBrush);
    }
    case WM_CTLCOLORLISTBOX: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, foregroundColor);
        SetBkColor(dc, panelColor);
        return reinterpret_cast<LRESULT>(panelBrush);
    }
    case WM_DRAWITEM: {
        const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
        if (item->CtlType == ODT_BUTTON)
            return drawButton(item);
        if (item->CtlType == ODT_STATIC && item->CtlID == 1100) {
            drawRuntimeStage(item);
            return TRUE;
        }
        if (item->CtlType == ODT_LISTBOX) {
            drawListItem(item);
            return TRUE;
        }
        break;
    }
    case WM_MEASUREITEM: {
        auto *item = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
        if (item->CtlType == ODT_LISTBOX)
            item->itemHeight = item->CtlID == idCategories ? 38 : 88;
        return TRUE;
    }
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (controlId == idEditor && (notification == EN_VSCROLL || notification == EN_HSCROLL)) {
            InvalidateRect(gutter, nullptr, TRUE);
            return 0;
        }
        if (controlId == idCategories && notification == LBN_SELCHANGE) {
            refreshExampleList();
            return 0;
        }
        if (controlId == idExamples && notification == LBN_SELCHANGE) {
            const int selected = static_cast<int>(SendMessageW(exampleList, LB_GETCURSEL, 0, 0));
            if (selected >= 0 && selected < static_cast<int>(visibleExamples.size()))
                loadPath(examplesPath / visibleExamples[static_cast<std::size_t>(selected)]->file);
            return 0;
        }
        if (controlId == idEditor && notification == EN_CHANGE && !suppressEditorChange) {
            documentDirty = true;
            updatePathLabel();
            SetTimer(window, highlightTimer, 180, nullptr);
            InvalidateRect(gutter, nullptr, TRUE);
            updateCursorLabel();
            return 0;
        }
        if (controlId == idExamplesFilter && notification == EN_CHANGE) {
            updateExampleFilter();
            return 0;
        }
        if (controlId == idSidebarToggle) {
            sidebarVisible = !sidebarVisible;
            SetWindowTextW(sidebarToggleButton, sidebarVisible ? L"탐색기 숨기기" : L"탐색기 표시");
            layoutControls(window);
            return 0;
        }
        if (controlId == idCodePage) {
            showWorkspacePage(WorkspacePage::Code);
            return 0;
        }
        if (controlId == idRuntimePage) {
            showWorkspacePage(WorkspacePage::Runtime);
            return 0;
        }
        if (controlId == idRefreshVariables) {
            refreshRuntimeVariables(true);
            return 0;
        }
        if (controlId == idNew) {
            newDocument();
            return 0;
        }
        if (controlId == idOpen) {
            openFile();
            return 0;
        }
        if (controlId == idOpenHaerye) {
            openHaeryeFile();
            return 0;
        }
        if (controlId == idSave) {
            saveCurrent();
            return 0;
        }
        if (controlId == idCheck) {
            Program program;
            std::string message;
            const bool valid = parseAndCheck(program, message);
            setText(output, toWide(message));
            setText(outputCaption, valid ? L"검사 통과" : L"오류");
            return 0;
        }
        if (controlId == idRun) {
            if (hasActiveRun)
                interpreter.requestStop();
            else
                runSource();
            return 0;
        }
        if (controlId == idConvert) {
            convertSurface();
            return 0;
        }
        if (controlId == idFindToggle) {
            toggleFindBar(false);
            return 0;
        }
        if (controlId == idFindNext) {
            findNext();
            return 0;
        }
        if (controlId == idReplace) {
            replaceCurrent();
            return 0;
        }
        if (controlId == idReplaceAll) {
            replaceAll();
            return 0;
        }
        if (controlId == idFindClose) {
            closeFindBar();
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        const auto *header = reinterpret_cast<const NMHDR *>(lParam);
        if (header && header->hwndFrom == editor) {
            if (header->code == EN_SELCHANGE) {
                updateCursorLabel();
                return 0;
            }
            if (header->code == EN_MSGFILTER) {
                const auto *filter = reinterpret_cast<const MSGFILTER *>(lParam);
                if (filter->msg == WM_VSCROLL || filter->msg == WM_MOUSEWHEEL ||
                    filter->msg == WM_KEYUP || filter->msg == WM_CHAR) {
                    InvalidateRect(gutter, nullptr, TRUE);
                    updateCursorLabel();
                }
            }
        }
        break;
    }
    case WM_TIMER:
        if (wParam == runTimer)
            pollRun();
        else if (wParam == engineTimer)
            updateEngine();
        else if (wParam == highlightTimer) {
            KillTimer(window, highlightTimer);
            applySyntaxColors();
            InvalidateRect(gutter, nullptr, TRUE);
        }
        return 0;
    case WM_CLOSE:
        if ((documentDirty && !engineProjectMode) || engineProjectDirty ||
            (standaloneSnapshotValid && standaloneDirtySnapshot)) {
            const int answer = MessageBoxW(window,
                L"저장하지 않은 변경 사항이 있습니다. 저장 버튼으로 저장하지 않고 종료할까요?",
                L"저장되지 않은 변경 사항", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
            if (answer != IDOK)
                return 0;
        }
        stopEngine();
        if (hasActiveRun)
            interpreter.stop();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, highlightTimer);
        webReady = false;
        webView.Reset();
        webController.Reset();
        webEnvironment.Reset();
        if (richEditModule)
            FreeLibrary(richEditModule);
        if (uiFont)
            DeleteObject(uiFont);
        if (editorFont)
            DeleteObject(editorFont);
        if (cardTitleFont)
            DeleteObject(cardTitleFont);
        if (cardDetailFont)
            DeleteObject(cardDetailFont);
        if (backgroundBrush)
            DeleteObject(backgroundBrush);
        if (panelBrush)
            DeleteObject(panelBrush);
        if (raisedBrush)
            DeleteObject(raisedBrush);
        if (stageBrush)
            DeleteObject(stageBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    richEditModule = LoadLibraryW(L"Msftedit.dll");
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = windowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = CreateSolidBrush(backgroundColor);
    type.lpszClassName = windowClass;
    type.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&type))
        return 1;
    const HWND window = CreateWindowExW(0, windowClass, L"Samat Studio", WS_OVERLAPPEDWINDOW,
                                        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 800, nullptr, nullptr,
                                        instance, nullptr);
    if (!window)
        return 1;
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (findBarVisible && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            (message.hwnd == findInput || message.hwnd == replaceInput)) {
            closeFindBar();
            continue;
        }
        if (findBarVisible && message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            (message.hwnd == findInput || message.hwnd == replaceInput)) {
            findNext((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(comResult))
        CoUninitialize();
    return static_cast<int>(message.wParam);
}
