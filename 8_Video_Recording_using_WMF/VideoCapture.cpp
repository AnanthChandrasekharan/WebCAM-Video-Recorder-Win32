// VideoCapture.cpp
#define NOMINMAX // Prevents min and max macros from being defined

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <windows.h>
#include <wrl/client.h>
#include <comdef.h>
#include <stdio.h>
#include <thread> // Add this line for std::thread

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

// Global variables
ComPtr<IMFSinkWriter> pSinkWriter = nullptr;
ComPtr<IMFSourceReader> pSourceReader = nullptr;
ComPtr<IMFMediaSource> pMediaSource = nullptr;
DWORD streamIndex = 0;
LONGLONG llSampleTime = 0;
LONGLONG llFrameDuration = 0;
bool isRecording = true; // Flag for recording status

// Error printing
void PrintErrorMessage(const char* msg, HRESULT hr) {
    _com_error err(hr);
    wprintf(L"%hs HRESULT: 0x%08lx (%hs)\n", msg, hr, err.ErrorMessage());
}

// Initialize Media Foundation
HRESULT InitializeMediaFoundation() {
    printf("Initializing Media Foundation...\n");
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to initialize Media Foundation.", hr);
    }
    return hr;
}

// Configure conservative media type (640x480 at 15 FPS, NV12 format)
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType) {
    printf("Configuring conservative media type...\n");
    HRESULT hr = MFCreateMediaType(&ppSelectedType);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create media type.", hr);
        return hr;
    }

    hr = ppSelectedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = ppSelectedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12); // Widely-supported uncompressed format
    hr = MFSetAttributeSize(ppSelectedType.Get(), MF_MT_FRAME_SIZE, 640, 480);
    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_FRAME_RATE, 30, 1); // 15 FPS for compatibility
    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set conservative media type attributes.", hr);
        return hr;
    }

    hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, ppSelectedType.Get());
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set current media type on source reader.", hr);
    }
    
    // Set frame duration for the selected frame rate
    llFrameDuration = (LONGLONG)(10'000'000.0 * 1 / 15); // 15 FPS
    return hr;
}

// Configure Sink Writer for MP4 Output
HRESULT ConfigureSinkWriter(ComPtr<IMFMediaType> pSourceType, ComPtr<IMFSinkWriter>& ppSinkWriter, DWORD& pStreamIndex) {
    printf("Configuring sink writer for output.mp4...\n");
    HRESULT hr = MFCreateSinkWriterFromURL(L"output.mp4", NULL, NULL, &ppSinkWriter);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create sink writer.", hr);
        return hr;
    }

    ComPtr<IMFMediaType> pMediaTypeOut = nullptr;
    hr = MFCreateMediaType(&pMediaTypeOut);
    pMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264); // MP4-compatible H.264 format
    pMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, 800000);

    MFSetAttributeSize(pMediaTypeOut.Get(), MF_MT_FRAME_SIZE, 640, 480);
    MFSetAttributeRatio(pMediaTypeOut.Get(), MF_MT_FRAME_RATE, 15, 1); // Matching 15 FPS
    MFSetAttributeRatio(pMediaTypeOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    pMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    ppSinkWriter->AddStream(pMediaTypeOut.Get(), &pStreamIndex);

    ComPtr<IMFMediaType> pMediaTypeIn = nullptr;
    hr = MFCreateMediaType(&pMediaTypeIn);
    pMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pMediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(pMediaTypeIn.Get(), MF_MT_FRAME_SIZE, 640, 480);
    MFSetAttributeRatio(pMediaTypeIn.Get(), MF_MT_FRAME_RATE, 15, 1); // Matching 15 FPS

    // Set Default Stride
    LONG stride = 0;
    hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_NV12.Data1, 640, &stride);
    if (SUCCEEDED(hr)) {
        hr = pMediaTypeIn->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set default stride on input media type.", hr);
        }
    }

    hr = ppSinkWriter->SetInputMediaType(pStreamIndex, pMediaTypeIn.Get(), NULL);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set input media type for sink writer.", hr);
        return hr;
    }

    hr = ppSinkWriter->BeginWriting();
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to begin writing with sink writer.", hr);
        ppSinkWriter.Reset();
    }
    return hr;
}

// Capture frames until stopped by Enter key press
void CaptureFrames() {
    printf("Capturing frames... Press Enter to stop recording.\n");
    HRESULT hr = S_OK;

    // Start a thread to detect Enter key press
    auto keyPressThread = std::thread([]() {
        getchar();
        isRecording = false; // Stop recording when Enter is pressed
    });

    while (isRecording) {
        ComPtr<IMFSample> pSample = nullptr;
        DWORD streamFlags = 0;

        hr = pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            NULL,
            &streamFlags,
            NULL,
            &pSample
        );

        if (FAILED(hr)) {
            PrintErrorMessage("Failed to read sample.", hr);
            break;
        }

        if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            printf("End of stream reached.\n");
            break;
        }

        if (pSample) {
            printf("Sample captured.\n");

            pSample->SetSampleTime(llSampleTime);
            pSample->SetSampleDuration(llFrameDuration);
            hr = pSinkWriter->WriteSample(streamIndex, pSample.Get());
            if (FAILED(hr)) {
                PrintErrorMessage("Failed to write sample to sink writer.", hr);
                break;
            }

            llSampleTime += llFrameDuration;
        } else {
            printf("No sample retrieved from source reader.\n");
        }
    }

    // Wait for key press thread to finish
    if (keyPressThread.joinable()) keyPressThread.join();

    printf("Finished capturing frames.\n");
}

// Start Recording
void StartRecording() {
    printf("Starting recording...\n");
    ComPtr<IMFAttributes> pAttributes;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** ppDevicesRaw = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes.Get(), &ppDevicesRaw, &count);
    if (count == 0) {
        printf("No video capture devices found.\n");
        CoTaskMemFree(ppDevicesRaw);
        return;
    }

    ComPtr<IMFActivate> pDevice = ppDevicesRaw[0];
    CoTaskMemFree(ppDevicesRaw);
    hr = pDevice->ActivateObject(IID_PPV_ARGS(&pMediaSource));
    hr = MFCreateSourceReaderFromMediaSource(pMediaSource.Get(), NULL, &pSourceReader);
    ComPtr<IMFMediaType> pSelectedType = nullptr;
    hr = ConfigureConservativeMediaType(pSourceReader, pSelectedType);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to configure conservative media type.", hr);
        return;
    }

    hr = ConfigureSinkWriter(pSelectedType, pSinkWriter, streamIndex);
    CaptureFrames();

    // Finalize and release resources
    if (pSinkWriter) {
        hr = pSinkWriter->Finalize();
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to finalize sink writer.", hr);
        }
        pSinkWriter.Reset();
    }
}

int main() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    InitializeMediaFoundation();
    StartRecording();
    MFShutdown();
    CoUninitialize();
    return 0;
}
