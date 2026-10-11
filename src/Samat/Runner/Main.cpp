#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "Samat/Haerye.hpp"
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr wchar_t windowClass[] = L"SamatRunnerWindow";
constexpr UINT_PTR gameTimer = 1;
constexpr float frameStep = 1.0F / 60.0F;
constexpr float moveSpeed = 6.2F;
constexpr float jumpSpeed = 8.8F;
constexpr float gravity = 22.0F;

HWND windowHandle{};
jm::HaeryeDocument scene;
std::filesystem::path scenePath;
std::size_t playerIndex{};
float playerX{}, playerY{}, velocityX{}, velocityY{}, spawnX{}, spawnY{};
bool grounded{}, won{};

COLORREF color(jm::HaeryeColor value) {
    return RGB(static_cast<BYTE>(std::clamp(value.r, 0.0F, 1.0F) * 255.0F),
               static_cast<BYTE>(std::clamp(value.g, 0.0F, 1.0F) * 255.0F),
               static_cast<BYTE>(std::clamp(value.b, 0.0F, 1.0F) * 255.0F));
}

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

bool overlaps(float leftA, float rightA, float bottomA, float topA,
              float leftB, float rightB, float bottomB, float topB) {
    return rightA > leftB && leftA < rightB && topA > bottomB && bottomA < topB;
}

void resetPlayer() {
    playerX = spawnX;
    playerY = spawnY;
    velocityX = velocityY = 0.0F;
    grounded = false;
}

bool loadScene(const std::filesystem::path &path, std::wstring &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = L"해례 파일을 읽을 수 없습니다.";
        return false;
    }
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    jm::HaeryeDocument candidate;
    jm::HaeryeDiagnostic diagnostic;
    if (!jm::parseHaerye(source, candidate, diagnostic) || !jm::validateHaerye(candidate, diagnostic)) {
        error = L"해례 오류: " + wide(diagnostic.toString());
        return false;
    }
    if (candidate.gameMode != jm::HaeryeGameMode::Platformer) {
        error = L"이 장면에는 게임 모드가 없습니다.\nscene 블록에 game: platformer를 지정해 주세요.";
        return false;
    }
    const auto found = std::find_if(candidate.objects.begin(), candidate.objects.end(), [](const auto &object) {
        return object.role == jm::HaeryeRole::Player;
    });
    if (found == candidate.objects.end()) {
        error = L"해례에 role: player 오브젝트가 없습니다.";
        return false;
    }
    scene = std::move(candidate);
    scenePath = path;
    playerIndex = static_cast<std::size_t>(std::distance(scene.objects.begin(), found));
    spawnX = (*scene.objects[playerIndex].position)[0];
    spawnY = (*scene.objects[playerIndex].position)[1];
    resetPlayer();
    return true;
}

std::optional<std::filesystem::path> chooseScene() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = nullptr;
    dialog.lpstrFilter = L"Haerye 게임 장면 (*.hy)\0*.hy\0모든 파일 (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = L"실행할 해례 게임을 선택하세요";
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(path);
}

void tick() {
    if (won) return;
    const bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000);
    const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000);
    velocityX = (right ? moveSpeed : 0.0F) - (left ? moveSpeed : 0.0F);

    const auto &player = scene.objects[playerIndex];
    const float halfWidth = (*player.size)[0] * 0.5F;
    const float halfHeight = (*player.size)[1] * 0.5F;
    const float oldBottom = playerY - halfHeight;
    playerX += velocityX * frameStep;
    velocityY -= gravity * frameStep;
    playerY += velocityY * frameStep;
    grounded = false;
    const float newBottom = playerY - halfHeight;

    for (const auto &object : scene.objects) {
        if (!object.role || !object.position || !object.size) continue;
        const auto [ox, oy] = *object.position;
        const auto [ow, oh] = *object.size;
        const float left = ox - ow * 0.5F, right = ox + ow * 0.5F;
        const float bottom = oy - oh * 0.5F, top = oy + oh * 0.5F;
        const float playerLeft = playerX - halfWidth, playerRight = playerX + halfWidth;
        if (*object.role == jm::HaeryeRole::Platform && velocityY <= 0.0F &&
            oldBottom >= top && newBottom <= top && playerRight > left && playerLeft < right) {
            playerY = top + halfHeight;
            velocityY = 0.0F;
            grounded = true;
        } else if (*object.role == jm::HaeryeRole::Hazard &&
                   overlaps(playerLeft, playerRight, newBottom, playerY + halfHeight, left, right, bottom, top)) {
            resetPlayer();
            return;
        } else if (*object.role == jm::HaeryeRole::Goal &&
                   overlaps(playerLeft, playerRight, newBottom, playerY + halfHeight, left, right, bottom, top)) {
            won = true;
            velocityX = velocityY = 0.0F;
            return;
        }
    }

    float lowest = playerY;
    for (const auto &object : scene.objects)
        if (object.position && object.size)
            lowest = std::min(lowest, (*object.position)[1] - (*object.size)[1]);
    if (playerY < lowest - 2.0F) resetPlayer();
}

struct View {
    float minX{}, maxX{}, minY{}, maxY{}, scale{};
    int centerX{}, centerY{};
    POINT point(float x, float y) const {
        return {centerX + static_cast<int>((x - (minX + maxX) * 0.5F) * scale),
                centerY - static_cast<int>((y - (minY + maxY) * 0.5F) * scale)};
    }
};

View makeView(int width, int height) {
    View view;
    view.minX = -8.0F; view.maxX = 8.0F; view.minY = -4.5F; view.maxY = 4.5F;
    for (const auto &object : scene.objects) {
        if (!object.position || !object.size) continue;
        const auto [x, y] = *object.position;
        const auto [w, h] = *object.size;
        view.minX = std::min(view.minX, x - w * 0.5F - 0.8F);
        view.maxX = std::max(view.maxX, x + w * 0.5F + 0.8F);
        view.minY = std::min(view.minY, y - h * 0.5F - 0.8F);
        view.maxY = std::max(view.maxY, y + h * 0.5F + 0.8F);
    }
    const int margin = 44;
    view.scale = std::min((width - margin * 2.0F) / (view.maxX - view.minX),
                          (height - margin * 2.0F) / (view.maxY - view.minY));
    view.centerX = width / 2;
    view.centerY = height / 2 + 12;
    return view;
}

void drawObject(HDC dc, const View &view, float x, float y, float width, float height,
                jm::HaeryeShape shape, jm::HaeryeColor fill) {
    const POINT center = view.point(x, y);
    const int halfWidth = std::max(2, static_cast<int>(width * view.scale * 0.5F));
    const int halfHeight = std::max(2, static_cast<int>(height * view.scale * 0.5F));
    HBRUSH brush = CreateSolidBrush(color(fill));
    HPEN pen = CreatePen(PS_SOLID, 1, color(fill));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    if (shape == jm::HaeryeShape::Circle)
        Ellipse(dc, center.x - halfWidth, center.y - halfHeight, center.x + halfWidth, center.y + halfHeight);
    else
        RoundRect(dc, center.x - halfWidth, center.y - halfHeight, center.x + halfWidth,
                  center.y + halfHeight, 8, 8);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void paint(HDC target, const RECT &bounds) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HBRUSH back = CreateSolidBrush(color(scene.backgroundColor));
    RECT local{0, 0, width, height};
    FillRect(dc, &local, back);
    DeleteObject(back);
    SetBkMode(dc, TRANSPARENT);
    const View view = makeView(width, height);

    for (std::size_t i = 0; i < scene.objects.size(); ++i) {
        const auto &object = scene.objects[i];
        if (!object.position || !object.size || !object.shape || i == playerIndex) continue;
        drawObject(dc, view, (*object.position)[0], (*object.position)[1], (*object.size)[0], (*object.size)[1],
                   *object.shape, *object.color);
    }
    const auto &player = scene.objects[playerIndex];
    drawObject(dc, view, playerX, playerY, (*player.size)[0], (*player.size)[1], *player.shape, *player.color);

    HFONT font = CreateFontW(20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(242, 239, 250));
    RECT title{24, 18, width - 24, 48};
    DrawTextW(dc, (wide(scene.name) + L"   ·   Samat Runner").c_str(), -1, &title,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(dc, RGB(205, 194, 229));
    RECT controls{24, height - 40, width - 24, height - 12};
    DrawTextW(dc, L"A / D 또는 ← / → 이동     Space 점프     R 다시 시작     Esc 닫기",
              -1, &controls, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (won) {
        SetTextColor(dc, RGB(255, 229, 160));
        RECT banner{width / 4, height / 2 - 24, width * 3 / 4, height / 2 + 24};
        DrawTextW(dc, L"완주!  Space를 눌러 다시 플레이", -1, &banner,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    BitBlt(target, bounds.left, bounds.top, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd, gameTimer, 16, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == gameTimer) {
            tick();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wParam == 'R' || (wParam == VK_SPACE && won)) {
            won = false;
            resetPlayer();
        } else if ((wParam == VK_SPACE || wParam == 'W' || wParam == VK_UP) && grounded && !won) {
            velocityY = jumpSpeed;
            grounded = false;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paintState{};
        HDC dc = BeginPaint(hwnd, &paintState);
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        paint(dc, bounds);
        EndPaint(hwnd, &paintState);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, gameTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    std::filesystem::path path;
    if (commandLine && *commandLine) {
        std::wstring argument(commandLine);
        if (argument.size() > 1 && argument.front() == L'"' && argument.back() == L'"')
            argument = argument.substr(1, argument.size() - 2);
        path = argument;
    }
    else if (auto selected = chooseScene()) path = *selected;
    else return 0;

    std::wstring error;
    if (!loadScene(path, error)) {
        MessageBoxW(nullptr, error.c_str(), L"Samat Runner", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW type{sizeof(type)};
    type.hInstance = instance;
    type.lpfnWndProc = windowProc;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = nullptr;
    type.lpszClassName = windowClass;
    type.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&type)) return 1;
    windowHandle = CreateWindowExW(0, windowClass, L"Samat Runner", WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr,
                                   instance, nullptr);
    if (!windowHandle) return 1;
    ShowWindow(windowHandle, showCommand);
    UpdateWindow(windowHandle);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
