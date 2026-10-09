#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "Samat/EditorSupport.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
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
constexpr UINT_PTR runTimer = 1;

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

constexpr std::array<Category, 6> categories{{
    {L"시작하기", L"Samat의 첫 코드"},
    {L"함수와 계산", L"함수 선언과 값 반환"},
    {L"조건과 반복", L"조건문, 범위, 반복문"},
    {L"訓C正音", L"한국어로 작성하는 코드"},
    {L"콘솔 입력", L"입력을 받는 작은 프로그램"},
    {L"네이티브", L"x64 백엔드 예제"},
}};

constexpr std::array<Example, 6> examples{{
    {L"Hello, Samat", L"시작하기", L"화면에 첫 문장을 출력합니다", L"hello.st"},
    {L"함수 맛보기", L"함수와 계산", L"factorial 함수와 결과 출력", L"functions.st"},
    {L"재귀 함수", L"함수와 계산", L"정수 함수의 네이티브 컴파일", L"native-factorial.st"},
    {L"조건과 반복", L"조건과 반복", L"if와 범위 반복으로 합계를 계산", L"conditions-loops.st"},
    {L"한국어 함수", L"訓C正音", L"같은 언어 기능을 한국어 문법으로 표현", L"functions-korean.st"},
    {L"대화형 계산기", L"콘솔 입력", L"readLine으로 입력받아 사칙 연산", L"calculator.st"},
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
HWND saveButton{};
HWND checkButton{};
HWND runButton{};
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
        if (category == L"모든 예제" || category == example.category)
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

bool saveCurrent();

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
    suppressEditorChange = true;
    setText(editor, toWide(source));
    suppressEditorChange = false;
    currentPath = path;
    documentDirty = false;
    setText(pathLabel, currentPath.filename().wstring());
    setText(output, L"예제를 열었습니다. 코드를 수정하고 검사하거나 실행할 수 있습니다.");
    setText(outputCaption, L"준비됨");
    return true;
}

std::string editorSource() { return toUtf8(windowText(editor)); }

bool parseAndCheck(Program &program, std::string &message) {
    Diagnostic parseDiagnostic;
    const auto source = editorSource();
    if (!parseCode(source, program, parseDiagnostic)) {
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
    setText(pathLabel, currentPath.filename().wstring());
    return true;
}

void openFile() {
    const auto selected = samat::editor::chooseOpenScriptFile(currentPath);
    if (selected)
        loadPath(*selected);
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
    setText(output, toWide(text));
    setText(outputCaption, result.error.empty() ? (result.cancelled ? L"중지됨" : L"완료") : L"오류");
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
    output = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                    idOutput);
    openButton = create(0, L"BUTTON", L"열기", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idOpen);
    saveButton = create(0, L"BUTTON", L"저장", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idSave);
    checkButton = create(0, L"BUTTON", L"검사", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idCheck);
    runButton = create(0, L"BUTTON", L"실행", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, idRun);

    uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    editorFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
                             L"Cascadia Mono");
    for (const auto control : {titleLabel, pathLabel, categoryCaption, examplesCaption, outputCaption,
                               categoryList, exampleList, openButton, saveButton, checkButton, runButton})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    SendMessageW(editor, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);
    SendMessageW(output, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);

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
    const int margin = 20;
    const int sidebarWidth = std::clamp(width / 5, 230, 300);
    const int gap = 18;
    const int topHeight = 78;
    const int outputHeight = std::clamp(height / 4, 150, 230);
    const int left = margin + sidebarWidth + gap;
    const int contentWidth = std::max(180, width - left - margin);
    const int editorTop = topHeight + 52;
    const int outputTop = height - outputHeight - margin;
    const int editorHeight = std::max(120, outputTop - editorTop - 14);

    MoveWindow(titleLabel, margin, 18, 260, 28, TRUE);
    MoveWindow(pathLabel, margin + 250, 22, std::max(180, width - margin - 900), 22, TRUE);
    const int buttonWidth = 78;
    const int buttonGap = 8;
    const int buttonsWidth = 4 * buttonWidth + 3 * buttonGap;
    int buttonX = std::max(left, width - margin - buttonsWidth);
    MoveWindow(openButton, buttonX, 18, buttonWidth, 38, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(saveButton, buttonX, 18, buttonWidth, 38, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(checkButton, buttonX, 18, buttonWidth, 38, TRUE);
    buttonX += buttonWidth + buttonGap;
    MoveWindow(runButton, buttonX, 18, buttonWidth, 38, TRUE);

    const int sidebarY = topHeight;
    MoveWindow(categoryCaption, margin, sidebarY, sidebarWidth, 24, TRUE);
    const int categoryHeight = std::clamp(height / 4, 140, 205);
    MoveWindow(categoryList, margin, sidebarY + 28, sidebarWidth, categoryHeight, TRUE);
    MoveWindow(examplesCaption, margin, sidebarY + 40 + categoryHeight, sidebarWidth, 24, TRUE);
    MoveWindow(exampleList, margin, sidebarY + 68 + categoryHeight,
               sidebarWidth, std::max(100, outputTop - sidebarY - categoryHeight - 82), TRUE);

    MoveWindow(editor, left, editorTop, contentWidth, editorHeight, TRUE);
    MoveWindow(outputCaption, left, outputTop, contentWidth, 24, TRUE);
    MoveWindow(output, left, outputTop + 28, contentWidth, outputHeight - 28, TRUE);
}

LRESULT drawButton(const DRAWITEMSTRUCT *item) {
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    const bool primary = item->CtlID == idRun;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const COLORREF fill = primary ? (disabled ? RGB(49, 77, 71) : accentColor)
                                  : (disabled ? RGB(31, 37, 46) : raisedColor);
    const COLORREF ink = primary ? RGB(10, 24, 25) : foregroundColor;
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
            setText(pathLabel, currentPath.filename().wstring() + L"  •  수정됨");
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
        break;
    }
    case WM_TIMER:
        if (wParam == runTimer)
            pollRun();
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
