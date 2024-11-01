#include <windows.h>     // For Windows API
#include <objbase.h>     // For CoCreateGuid and GUID handling
#include <iostream>      // For standard I/O
#include <string>        // For string handling

std::string GenerateGUIDInRegistryFormat() {
    // Initialize COM library, necessary for using CoCreateGuid on some systems
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to initialize COM library.");
    }

    GUID guid;
    std::string result;

    // Create a new GUID
    if (CoCreateGuid(&guid) == S_OK) {
        wchar_t guidString[39]; // GUID length in registry format is 38 characters + null terminator

        // Convert GUID to a wide-character string in registry format
        if (StringFromGUID2(guid, guidString, 39) > 0) {
            // Convert the wide string to a narrow string for standard output
            char narrowString[39];
            wcstombs(narrowString, guidString, 39);

            // Store the GUID in registry format
            result = narrowString;
        } else {
            CoUninitialize();
            throw std::runtime_error("Failed to convert GUID to string format.");
        }
    } else {
        CoUninitialize();
        throw std::runtime_error("Failed to create GUID.");
    }

    // Clean up COM library
    CoUninitialize();
    
    return result;
}

int main() {
    try {
        // Call the function and print the GUID
        std::string guidInRegistryFormat = GenerateGUIDInRegistryFormat();
        std::cout << "GUID in Registry Format: " << guidInRegistryFormat << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    
    return 0;
}
