#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <cstdio>
#include <thread>
#include <algorithm>
#include <tlhelp32.h>
#include "discord_game_sdk.h"
#include "discord.h"

NOTIFYICONDATA nid;
bool running = true;

std::wstring GetActiveWindowTitle() {
    wchar_t windowTitle[256];
    HWND activeWindow = GetForegroundWindow();
    if (activeWindow && GetWindowTextW(activeWindow, windowTitle, sizeof(windowTitle) / sizeof(wchar_t))) {
        std::wstring title(windowTitle);
        //Hard-coded for Codelite
        if (!title.empty() && title[0] == L'[' && title.find(L']') != std::wstring::npos) {
            title = L"Codelite";
        }
        //All WSL apps have (Debian) in name
        size_t pos = title.find(L" (Debian)");
        if (pos != std::wstring::npos) {
            title = title.substr(0, pos);
        }
        return title;
    }
    return L"";
}

std::vector<std::string> RunWSLCommand(const std::string& command) {
    std::vector<std::string> output;
    HANDLE hStdOutRead, hStdOutWrite;
    HANDLE hStdErrRead, hStdErrWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0) ||
        !CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) {
        return output;
    }

    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFO si = { sizeof(STARTUPINFO) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdErrWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::string fullCommand = "wsl " + command;

    if (!CreateProcessW(
        NULL,
        const_cast<LPWSTR>(std::wstring(fullCommand.begin(), fullCommand.end()).c_str()),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        return output;
    }

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdErrWrite);
    char buffer[256] = {};
    DWORD bytesRead;

    while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output.emplace_back(buffer);
    }

    CloseHandle(hStdOutRead);
    CloseHandle(hStdErrRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return output;
}

std::string ToLowercase(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

bool IsActiveWindowWSL(const std::wstring& activeWindowTitle, const std::vector<std::string>& wslProcesses) {
    std::string activeWindowLower(ToLowercase(std::string(activeWindowTitle.begin(), activeWindowTitle.end())));
    for (const auto& line : wslProcesses) {
        if (ToLowercase(line).find(activeWindowLower) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void InitTrayIcon(HINSTANCE hInstance, HWND hWnd) {
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = (HICON)LoadImage(hInstance, L"wsl2rpc.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

    if (!nid.hIcon) {
        MessageBox(NULL, L"Error: Could not load tray icon.", L"Error", MB_ICONERROR);
        return;
    }

    wcscpy_s(nid.szTip, L"WSL Discord RPC");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void ShowAboutWindow(HINSTANCE hInstance, HWND hParent) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (message == WM_CLOSE || (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) {
            DestroyWindow(hWnd);
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    };
    wc.hInstance = hInstance;
    wc.hIcon = (HICON)LoadImage(hInstance, L"wsl2rpc.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    wc.lpszClassName = L"AboutWindow";

    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(
        0, wc.lpszClassName, L"About wsl2rpc",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        hParent, NULL, hInstance, NULL
    );

    HFONT hFont = CreateFont(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    HWND hText = CreateWindowEx(
        0, L"STATIC", L"Discord Rich Presence for WSL2 made in C++\nMade by Mokona59\nV1.0",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 10, 380, 100,
        hWnd, NULL, hInstance, NULL
    );

    SendMessage(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

    RECT textRect;
    GetWindowRect(hText, &textRect);
    int textHeight = textRect.bottom - textRect.top;
    RECT windowRect;
    GetClientRect(hWnd, &windowRect);

    int spaceBelowText = windowRect.bottom - textHeight - 20;
    int iconWidth = 128;
    int iconHeight = 128;
    int iconX = (windowRect.right - iconWidth) / 2;
    int iconY = textHeight + (spaceBelowText - iconHeight) / 2;

    HWND hIcon = CreateWindowEx(
        0, L"STATIC", NULL,
        WS_CHILD | WS_VISIBLE | SS_ICON,
        iconX, iconY, iconWidth, iconHeight,
        hWnd, NULL, hInstance, NULL
    );

    HICON hLoadedIcon = (HICON)LoadImage(hInstance, L"wsl2rpc.ico", IMAGE_ICON, iconWidth, iconHeight, LR_LOADFROMFILE);
    SendMessage(hIcon, STM_SETICON, (WPARAM)hLoadedIcon, 0);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    DeleteObject(hFont);
}

void ShowTrayMenu(HWND hWnd) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, 1, L"About");
    AppendMenu(hMenu, MF_STRING, 2, L"Exit");

    SetForegroundWindow(hWnd);
    int selection = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, cursorPos.x, cursorPos.y, 0, hWnd, NULL);

    if (selection == 1) {
        ShowAboutWindow(GetModuleHandle(NULL), hWnd);
    }
    else if (selection == 2) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
    }

    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_APP + 1) {
        switch (lParam) {
        case WM_RBUTTONDOWN:
            ShowTrayMenu(hWnd);
            break;
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool IsDiscordRunning() {
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe32 = {};
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"Discord.exe") == 0) {
                CloseHandle(hProcessSnap);
                return true;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    return false;
}

 int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (!IsDiscordRunning()) {
        MessageBox(NULL, L"Discord is not running. Please start Discord to enable Rich Presence.", L"wsl2rpc", MB_ICONWARNING);
        return 0;
    }

    discord::Core* core = nullptr;
    auto result = discord::Core::Create(YOUR_BOT_CLIENT_ID,
        DiscordCreateFlags_Default,
        &core);
    if (result != discord::Result::Ok) {
        MessageBox(NULL, L"Error initializing Discord Core", L"Error", MB_ICONERROR);
        return -1;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"wsl2rpc";
    RegisterClass(&wc);

    HWND hWnd = CreateWindowEx(0, wc.lpszClassName, L"wsl2rpc", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    InitTrayIcon(hInstance, hWnd);

    static int counter = 0;
    bool previousIsWSL = false;

    while (running) {
        MSG msg = {};
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT) {
                running = false;
            }
        }

        std::wstring activeWindowTitle = GetActiveWindowTitle();
        if (activeWindowTitle.empty()) {
            continue;
        }

        std::vector<std::string> wslProcesses = RunWSLCommand("ps e");
        if (wslProcesses.empty()) {
            continue;
        }

        bool isWSL = IsActiveWindowWSL(activeWindowTitle, wslProcesses);

        if (isWSL) {
            if (!previousIsWSL) {
                counter = 0;
            }
            counter++;
        }
        else {
            counter = 0;
        }

        previousIsWSL = isWSL;

        if (core) {
            discord::Activity activity{};
            activity.SetState(isWSL ? "WSL" : "Windows");
            activity.SetDetails(isWSL ? ("Using " + std::string(activeWindowTitle.begin(), activeWindowTitle.end())).c_str() : "Working on Windows application");
            activity.GetAssets().SetLargeImage(isWSL ? "debian" : "windows");
            activity.GetAssets().SetLargeText(isWSL ? "WSL detected" : "Windows detected");

            core->ActivityManager().UpdateActivity(activity, nullptr);
            core->RunCallbacks();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}
