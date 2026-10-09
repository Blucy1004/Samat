#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "Samat/EditorSupport.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
using namespace jm::script;

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
constexpr UINT_PTR runTimer = 1;
constexpr UINT_PTR highlightTimer = 2;

constexpr COLORREF backgroundColor = RGB(15, 18, 24);
constexpr COLORREF panelColor = RGB(23, 28, 37);
constexpr COLORREF raisedColor = RGB(30, 37, 48);
constexpr COLORREF borderColor = RGB(42, 51, 65);
constexpr COLORREF foregroundColor = RGB(230, 237, 245);
constexpr COLORREF mutedColor = RGB(142, 155, 173);
constexpr COLORREF accentColor = RGB(83, 221, 183);
constexpr COLORREF selectedColor = RGB(34, 62, 62);

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

constexpr std::array<Category, 7> categories{{
    {L"시작하기", L"Samat의 첫 코드"},
    {L"함수와 계산", L"함수 선언과 값 반환"},
    {L"조건과 반복", L"조건문, 범위, 반복문"},
    {L"訓C正音", L"한국어로 작성하는 코드"},
    {L"콘솔 입력", L"입력을 받는 작은 프로그램"},
    {L"네이티브", L"x64 백엔드 예제"},
    {L"문자열", L"문자열 안에 값을 표현"},
}};

constexpr std::array<Example, 7> examples{{
    {L"Hello, Samat", L"시작하기", L"화면에 첫 문장을 출력합니다", L"hello.st"},
    {L"함수 맛보기", L"함수와 계산", L"factorial 함수와 결과 출력", L"functions.st"},
    {L"재귀 함수", L"함수와 계산", L"정수 함수의 네이티브 컴파일", L"native-factorial.st"},
    {L"조건과 반복", L"조건과 반복", L"if와 범위 반복으로 합계를 계산", L"conditions-loops.st"},
    {L"한국어 함수", L"訓C正音", L"같은 언어 기능을 한국어 문법으로 표현", L"functions-korean.st"},
    {L"대화형 계산기", L"콘솔 입력", L"readLine으로 입력받아 사칙 연산", L"calculator.st"},
    {L"문자열 보간", L"문자열", L"f-문자열에 변수와 식을 넣어 출력", L"string-interpolation.st"},
}};

HWND mainWindow{};
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
std::filesystem::path examplesPath;
std::filesystem::path currentPath;
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
int sidebarWidthSetting{250};
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

const char *surfaceName(Surface surface) { return surface == Surface::Code ? "Code Syntax" : "訓C正音"; }

void updatePathLabel() {
    const std::wstring name = currentPath.empty() ? L"새 문서" : currentPath.filename().wstring();
    const std::wstring mode = currentSurface == Surface::Code ? L"Code Syntax" : L"訓C正音";
    setText(pathLabel, mode + L"  ·  " + name + (documentDirty ? L"  •  수정됨" : L""));
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
    if (documentDirty) {
        const int choice = MessageBoxW(mainWindow, L"현재 파일에 저장하지 않은 변경 사항이 있습니다.\n저장할까요?",
                                       L"변경 사항 저장", MB_YESNOCANCEL | MB_ICONWARNING);
        if (choice == IDCANCEL)
            return false;
        if (choice == IDYES && !saveCurrent())
            return false;
    }
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
    SetTimer(mainWindow, highlightTimer, 1, nullptr);
    InvalidateRect(gutter, nullptr, TRUE);
    setText(output, L"예제를 열었습니다. 코드를 수정하고 검사하거나 실행할 수 있습니다.");
    setText(outputCaption, L"준비됨");
    return true;
}

std::string editorSource() { return toUtf8(windowText(editor)); }

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
        row.nameLabel = CreateWindowExW(0, L"STATIC", toWide(row.name).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        0, 0, 0, 0, mainWindow,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(baseId)),
                                        GetModuleHandleW(nullptr), nullptr);
        const std::wstring typeLabel = L"초기값 · " + toWide(typeName(row.type));
        row.currentLabel = CreateWindowExW(0, L"STATIC", typeLabel.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           0, 0, 0, 0, mainWindow,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(baseId + 1)),
                                           GetModuleHandleW(nullptr), nullptr);
        row.valueEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", toWide(defaultVariableText(statement)).c_str(),
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, mainWindow,
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
    documentDirty = true;
    updatePathLabel();
    SetTimer(mainWindow, highlightTimer, 1, nullptr);
    InvalidateRect(gutter, nullptr, TRUE);
    setText(output, L"문법을 " + toWide(surfaceName(currentSurface)) +
                        L"로 변환했습니다. Ctrl+Z로 되돌릴 수 있습니다.");
    setText(outputCaption, L"변환 완료");
    const LONG newCaret = std::min<LONG>(selection.cpMin, static_cast<LONG>(convertedWide.size()));
    SendMessageW(editor, EM_SETSEL, newCaret, newCaret);
}

void newDocument() {
    if (documentDirty) {
        const int choice = MessageBoxW(mainWindow, L"저장하지 않은 변경 사항이 있습니다.\n새 문서를 열까요?",
                                       L"새 문서", MB_YESNOCANCEL | MB_ICONWARNING);
        if (choice == IDCANCEL || (choice == IDYES && !saveCurrent()))
            return;
    }
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
        return;
    }
    const auto source = editorSource();
    if (source.find("readLine(") != std::string::npos || source.find("readLine ()") != std::string::npos) {
        setText(output, L"이 예제는 콘솔 입력을 사용합니다. 현재 Studio 실행은 readLine 입력을 받지 않으므로\n저장한 뒤 명령줄에서 실행해 주세요.");
        setText(outputCaption, L"콘솔 입력 예제");
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
        return;
    }
    hasActiveRun = true;
    SetWindowTextW(runButton, L"중지");
    EnableWindow(checkButton, FALSE);
    setText(output, L"실행 중…");
    setText(outputCaption, L"실행 중");
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
}

COLORREF syntaxColor(samat::editor::SyntaxKind kind) {
    switch (kind) {
    case samat::editor::SyntaxKind::Keyword:
        return RGB(198, 120, 221);
    case samat::editor::SyntaxKind::Builtin:
        return RGB(86, 182, 194);
    case samat::editor::SyntaxKind::Number:
        return RGB(209, 154, 102);
    case samat::editor::SyntaxKind::String:
        return RGB(152, 195, 121);
    case samat::editor::SyntaxKind::Comment:
        return RGB(120, 130, 143);
    case samat::editor::SyntaxKind::Operator:
        return RGB(171, 178, 191);
    case samat::editor::SyntaxKind::Identifier:
        return foregroundColor;
    }
    return foregroundColor;
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
    categoryList = create(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                          idCategories);
    exampleList = create(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                             LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                         idExamples);
    editor = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                    idEditor);
    if (richEditModule) {
        DestroyWindow(editor);
        editor = create(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                            WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN |
                            ES_NOHIDESEL,
                        idEditor);
    }
    output = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                    idOutput);
    newButton = create(0, L"BUTTON", L"새 문서", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idNew);
    openButton = create(0, L"BUTTON", L"열기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idOpen);
    saveButton = create(0, L"BUTTON", L"저장", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idSave);
    checkButton = create(0, L"BUTTON", L"검사", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idCheck);
    runButton = create(0, L"BUTTON", L"실행", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idRun);
    convertButton = create(0, L"BUTTON", L"訓C正音으로", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idConvert);
    findButton = create(0, L"BUTTON", L"찾기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idFindToggle);
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
    codePageButton = create(0, L"BUTTON", L"코드 편집", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idCodePage);
    runtimePageButton = create(0, L"BUTTON", L"런타임 변수", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idRuntimePage);
    runtimeStage = create(WS_EX_CLIENTEDGE, L"STATIC", L"실행 화면", WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
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

    uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    editorFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
                             L"Cascadia Mono");
    for (const auto control : {titleLabel, pathLabel, categoryCaption, examplesCaption, outputCaption,
                               categoryList, exampleList, newButton, openButton, saveButton, checkButton,
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
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = 16;
    const bool compact = width < 1060;
    const int topHeight = compact ? 112 : 94;
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
    const int buttonWidth = compact ? 75 : 82;
    const int buttonGap = 6;
    int buttonX = margin;
    const int toolbarY = 49;
    MoveWindow(sidebarToggleButton, buttonX, toolbarY, 124, 34, TRUE);
    buttonX += 130;
    MoveWindow(newButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(openButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(saveButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(checkButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(runButton, buttonX, toolbarY, buttonWidth, 34, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(convertButton, buttonX, toolbarY, 104, 34, TRUE);
    buttonX += 104 + buttonGap;
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
        const int categoryHeight = std::clamp((outputTop - sidebarY) / 4, 100, 170);
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
        const int inspectorWidth = std::clamp(width / 4, 290, 390);
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
}

LRESULT drawButton(const DRAWITEMSTRUCT *item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool primary = item->CtlID == idRun;
    const bool selectedPage = (item->CtlID == idCodePage && workspacePage == WorkspacePage::Code) ||
                              (item->CtlID == idRuntimePage && workspacePage == WorkspacePage::Runtime);
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = (primary || selectedPage) ? (disabled ? RGB(49, 77, 71) : accentColor)
                                  : (disabled ? RGB(31, 37, 46) : raisedColor);
    const COLORREF ink = (primary || selectedPage) ? RGB(10, 24, 25) : foregroundColor;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? accentColor : borderColor);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    DrawTextW(dc, windowText(item->hwndItem).c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    textRect.left += 14;
    textRect.right -= 8;
    textRect.top += isCategory ? 7 : 7;
    wchar_t text[192]{};
    SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(text));
    DrawTextW(item->hDC, text, -1, &textRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!isCategory && item->itemID < visibleExamples.size()) {
        SetTextColor(item->hDC, mutedColor);
        textRect.top += 21;
        DrawTextW(item->hDC, visibleExamples[item->itemID]->summary, -1, &textRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
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
        createControls(window);
        examplesPath = discoverExamplesPath();
        if (!visibleExamples.empty())
            loadPath(examplesPath / visibleExamples.front()->file);
        return 0;
    case WM_SIZE:
        layoutControls(window);
        return 0;
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
        SetTextColor(dc, (reinterpret_cast<HWND>(lParam) == pathLabel) ? mutedColor : foregroundColor);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, foregroundColor);
        SetBkColor(dc, raisedColor);
        return reinterpret_cast<LRESULT>(raisedBrush);
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
        if (item->CtlType == ODT_LISTBOX) {
            drawListItem(item);
            return TRUE;
        }
        break;
    }
    case WM_MEASUREITEM: {
        auto *item = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
        if (item->CtlType == ODT_LISTBOX)
            item->itemHeight = item->CtlID == idCategories ? 36 : 52;
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
        else if (wParam == highlightTimer) {
            KillTimer(window, highlightTimer);
            applySyntaxColors();
            InvalidateRect(gutter, nullptr, TRUE);
        }
        return 0;
    case WM_CLOSE:
        if (documentDirty) {
            const int choice = MessageBoxW(window, L"저장하지 않은 변경 사항이 있습니다.\n저장하고 닫을까요?",
                                           L"변경 사항 저장", MB_YESNOCANCEL | MB_ICONWARNING);
            if (choice == IDCANCEL)
                return 0;
            if (choice == IDYES && !saveCurrent())
                return 0;
        }
        if (hasActiveRun)
            interpreter.stop();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, highlightTimer);
        if (richEditModule)
            FreeLibrary(richEditModule);
        if (uiFont)
            DeleteObject(uiFont);
        if (editorFont)
            DeleteObject(editorFont);
        if (backgroundBrush)
            DeleteObject(backgroundBrush);
        if (panelBrush)
            DeleteObject(panelBrush);
        if (raisedBrush)
            DeleteObject(raisedBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
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
    return static_cast<int>(message.wParam);
}
