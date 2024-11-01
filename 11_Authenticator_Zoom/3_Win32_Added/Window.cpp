#include <windows.h>
#include <objbase.h>
#include <string>
#include <iostream>

// Global variables for controls
HWND hButtonGenerate, hButtonCopy, hText;

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

// Function to copy text to clipboard
void CopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();

        // Allocate global memory for the text
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t* pGlobal = static_cast<wchar_t*>(GlobalLock(hGlobal));
            if (pGlobal) {
                wcscpy_s(pGlobal, text.size() + 1, text.c_str());
                GlobalUnlock(hGlobal);

                // Set the text to the clipboard
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
    }
}

// Window procedure for handling messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create "CreateSessionID" button
            hButtonGenerate = CreateWindow(
                L"BUTTON", L"CreateSessionID",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                50, 50, 150, 30,
                hwnd, (HMENU) 1, (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
            );

            // Create "Copy UUID" button
            hButtonCopy = CreateWindow(
                L"BUTTON", L"Copy UUID",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                220, 50, 100, 30,
                hwnd, (HMENU) 2, (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
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
            if (LOWORD(wParam) == 1) {  // "CreateSessionID" button click
                std::wstring guid = GenerateGUIDInRegistryFormat();
                SetWindowText(hText, guid.c_str());  // Display GUID in the static text control
            } else if (LOWORD(wParam) == 2) {  // "Copy UUID" button click
                wchar_t guidText[39];
                GetWindowText(hText, guidText, 39);
                CopyToClipboard(hwnd, guidText);  // Copy text from static control to clipboard
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
