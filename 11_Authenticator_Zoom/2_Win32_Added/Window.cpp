#include <windows.h>
#include <objbase.h>
#include <string>
#include <iostream>

// Global variables for controls
HWND hButton, hText;

// Function to generate GUID in registry format
std::wstring GenerateGUIDInRegistryFormat() {
    GUID guid;
    if (CoCreateGuid(&guid) == S_OK) {
        wchar_t guidString[39];
        if (StringFromGUID2(guid, guidString, 39) > 0) {
            return std::wstring(guidString);
        }
    }
    return L"Failed to generate GUID.";
}

// Window procedure for handling messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create "CreateSessionID" button
            hButton = CreateWindow(
                L"BUTTON", L"CreateSessionID",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                50, 50, 150, 30,
                hwnd, (HMENU) 1, (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
            );

            // Create static text control to display the GUID
            hText = CreateWindow(
                L"STATIC", L"",
                WS_VISIBLE | WS_CHILD,
                50, 100, 300, 30,
                hwnd, NULL, (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
            );
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == 1) {  // Button click with ID 1
                std::wstring guid = GenerateGUIDInRegistryFormat();
                SetWindowText(hText, guid.c_str());  // Display GUID in the static text control
            }
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Main function for creating the window
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize COM library
    CoInitialize(NULL);

    // Window class setup
    const wchar_t CLASS_NAME[] = L"GUID Generator Window";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"GUID Generator",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 200,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        CoUninitialize();
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up COM library
    CoUninitialize();
    return 0;
}
