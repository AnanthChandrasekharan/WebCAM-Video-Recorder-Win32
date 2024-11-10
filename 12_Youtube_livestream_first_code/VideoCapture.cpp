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
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <iostream>
#include <limits> // For std::numeric_limits

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

// Constants
const UINT32 FRAME_WIDTH = 640;
const UINT32 FRAME_HEIGHT = 480;
const UINT32 FRAME_RATE_NUMERATOR = 60;
const UINT32 FRAME_RATE_DENOMINATOR = 1;
const UINT64 FRAME_DURATION = 10'000'000 / FRAME_RATE_NUMERATOR;
const UINT32 VIDEO_BITRATE = 800000;
const UINT32 AUDIO_SAMPLE_RATE = 48000;
const UINT32 AUDIO_CHANNELS = 2;
const UINT32 AUDIO_BITS_PER_SAMPLE = 16;
const UINT32 AUDIO_BLOCK_ALIGNMENT = AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8);
const UINT32 AUDIO_AVG_BYTES_PER_SECOND = AUDIO_SAMPLE_RATE * AUDIO_BLOCK_ALIGNMENT;

// Global variables
ComPtr<IMFSourceReader> pVideoSourceReader = nullptr;
ComPtr<IMFSourceReader> pAudioSourceReader = nullptr;
ComPtr<IMFMediaSource> pVideoMediaSource = nullptr;
ComPtr<IMFMediaSource> pAudioMediaSource = nullptr;
DWORD videoStreamIndex = 0;
DWORD audioStreamIndex = 1;
std::atomic<bool> isRecording(true);

FILE* ffmpegProcess = nullptr;
const std::string STREAM_KEY = "0a19-6hgw-1225-zqws-c4mw";
const std::string STREAM_URL = "rtmp://a.rtmp.youtube.com/live2";

// Device Info structure for selection
struct DeviceInfo {
    ComPtr<IMFActivate> device;
    std::wstring name;
};

// Error printing
void PrintErrorMessage(const char* msg, HRESULT hr) {
    _com_error err(hr);
    wprintf(L"%hs HRESULT: 0x%08lx (%hs)\n", msg, hr, err.ErrorMessage());
}

// Function declarations
HRESULT InitializeMediaFoundation();
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType);
void CaptureFrames();
void StartRecording();
HRESULT EnumerateDevices(GUID sourceType, std::vector<DeviceInfo>& devices);
void ListDevices(const std::vector<DeviceInfo>& devices);
ComPtr<IMFMediaSource> SelectDevice(const std::vector<DeviceInfo>& devices);
void ClearInputBuffer();
void StartFFmpegProcess();
void StopFFmpegProcess();

// Initialize Media Foundation
HRESULT InitializeMediaFoundation() {
    printf("Initializing Media Foundation...\n");
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to initialize Media Foundation.", hr);
    }
    return hr;
}

// Configure video media type
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType) {
    HRESULT hr = MFCreateMediaType(&ppSelectedType);
    if (SUCCEEDED(hr)) hr = ppSelectedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = ppSelectedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(ppSelectedType.Get(), MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_FRAME_RATE, FRAME_RATE_NUMERATOR, FRAME_RATE_DENOMINATOR);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, ppSelectedType.Get());
    if (FAILED(hr)) PrintErrorMessage("Failed to configure video media type.", hr);
    return hr;
}

// Start FFmpeg process
void StartFFmpegProcess() {
    std::string command = "ffmpeg -y -f rawvideo -pix_fmt nv12 -s 640x480 -r 60 -i - "
                          "-f lavfi -i anullsrc=channel_layout=stereo:sample_rate=48000 "
                          "-c:v libx264 -pix_fmt yuv420p -preset veryfast -g 120 -b:v 800k "
                          "-c:a aac -b:a 160k -ar 44100 -f flv " + STREAM_URL + "/" + STREAM_KEY;

    ffmpegProcess = _popen(command.c_str(), "wb");
    if (!ffmpegProcess) {
        std::cerr << "Failed to start FFmpeg process.\n";
    }
}

// Stop FFmpeg process
void StopFFmpegProcess() {
    if (ffmpegProcess) {
        _pclose(ffmpegProcess);
        ffmpegProcess = nullptr;
    }
}

// Capture frames until stopped by Enter key press
void CaptureFrames() {
    printf("Capturing frames... Press Enter to stop recording.\n");
    HRESULT hr = S_OK;
    LONGLONG startTime = 0;

    auto keyPressThread = std::thread([]() {
        getchar(); // Wait for Enter key press
        isRecording = false;
    });

    StartFFmpegProcess();  // Start FFmpeg process for streaming

    while (isRecording) {
        auto frameStart = std::chrono::steady_clock::now();

        if (startTime == 0) {
            startTime = MFGetSystemTime();
        }

        // Capture Video Sample
        ComPtr<IMFSample> pVideoSample;
        DWORD videoStreamFlags = 0;
        hr = pVideoSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            NULL,
            &videoStreamFlags,
            NULL,
            &pVideoSample
        );

        if (videoStreamFlags & MF_SOURCE_READERF_STREAMTICK) {
            printf("Video stream tick detected\n");
        }

        if (pVideoSample) {
            ComPtr<IMFMediaBuffer> pBuffer;
            pVideoSample->ConvertToContiguousBuffer(&pBuffer);
            BYTE* pData = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            pBuffer->Lock(&pData, &maxLength, &currentLength);

            // Write the video data to FFmpeg
            if (ffmpegProcess) {
                fwrite(pData, 1, currentLength, ffmpegProcess);
                fflush(ffmpegProcess);
            }

            pBuffer->Unlock();
        }

        // Enforce frame duration for 60 FPS
        auto frameEnd = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::micro> frameTime = frameEnd - frameStart;
        auto frameWait = std::chrono::microseconds(1000000 / FRAME_RATE_NUMERATOR) - frameTime;
        if (frameWait > std::chrono::microseconds(0)) {
            std::this_thread::sleep_for(frameWait);
        }
    }

    if (keyPressThread.joinable()) keyPressThread.join();
    printf("Finished capturing frames.\n");

    StopFFmpegProcess();  // Stop FFmpeg process after recording

    pVideoSourceReader.Reset();
    pAudioSourceReader.Reset();
}

// Enumerate available devices
HRESULT EnumerateDevices(GUID sourceType, std::vector<DeviceInfo>& devices) {
    ComPtr<IMFAttributes> pAttributes;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) return hr;

    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, sourceType);
    if (FAILED(hr)) return hr;

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    hr = MFEnumDeviceSources(pAttributes.Get(), &ppDevices, &deviceCount);
    if (FAILED(hr)) return hr;

    for (UINT32 i = 0; i < deviceCount; i++) {
        DeviceInfo info;
        info.device = ppDevices[i];

        WCHAR* deviceName = nullptr;
        UINT32 nameLength = 0;
        hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceName, &nameLength);
        if (SUCCEEDED(hr)) {
            info.name = deviceName;
            CoTaskMemFree(deviceName);
            devices.push_back(info);
        } else {
            CoTaskMemFree(deviceName);
        }
    }

    CoTaskMemFree(ppDevices);
    return S_OK;
}

// Display device list for selection
void ListDevices(const std::vector<DeviceInfo>& devices) {
    for (size_t i = 0; i < devices.size(); ++i) {
        std::wcout << i << L": " << devices[i].name << std::endl;
    }
}

// Function to clear the input buffer
void ClearInputBuffer() {
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

// Select device by index
ComPtr<IMFMediaSource> SelectDevice(const std::vector<DeviceInfo>& devices) {
    int selection;
    std::wcout << L"Select device by index: ";
    std::cin >> selection;

    // Clear the input buffer to remove any leftover characters
    ClearInputBuffer();

    if (selection >= 0 && selection < static_cast<int>(devices.size())) {
        ComPtr<IMFMediaSource> mediaSource;
        devices[selection].device->ActivateObject(IID_PPV_ARGS(&mediaSource));
        return mediaSource;
    } else {
        std::wcout << L"Invalid selection." << std::endl;
        return nullptr;
    }
}

// Start Recording
void StartRecording() {
    printf("Starting recording...\n");

    // Enumerate and select video device
    std::vector<DeviceInfo> videoDevices;
    HRESULT hr = EnumerateDevices(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID, videoDevices);
    if (FAILED(hr) || videoDevices.empty()) {
        printf("No video capture devices found.\n");
        return;
    }

    printf("Available Video Devices:\n");
    ListDevices(videoDevices);
    pVideoMediaSource = SelectDevice(videoDevices);
    if (!pVideoMediaSource) return;

    // Create video source reader
    hr = MFCreateSourceReaderFromMediaSource(pVideoMediaSource.Get(), NULL, &pVideoSourceReader);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create video source reader.", hr);
        return;
    }

    ComPtr<IMFMediaType> pSelectedVideoType;
    hr = ConfigureConservativeMediaType(pVideoSourceReader, pSelectedVideoType);

    CaptureFrames();
}

int main() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to initialize COM library.", hr);
        return -1;
    }

    hr = InitializeMediaFoundation();
    if (SUCCEEDED(hr)) StartRecording();
    MFShutdown();
    CoUninitialize();
    return 0;
}
