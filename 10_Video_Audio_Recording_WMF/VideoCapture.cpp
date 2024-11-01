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
ComPtr<IMFSinkWriter> pSinkWriter = nullptr;
ComPtr<IMFSourceReader> pVideoSourceReader = nullptr;
ComPtr<IMFSourceReader> pAudioSourceReader = nullptr;
ComPtr<IMFMediaSource> pVideoMediaSource = nullptr;
ComPtr<IMFMediaSource> pAudioMediaSource = nullptr;
DWORD videoStreamIndex = 0;
DWORD audioStreamIndex = 1;
std::atomic<bool> isRecording(true);

// Error printing
void PrintErrorMessage(const char* msg, HRESULT hr) {
    _com_error err(hr);
    wprintf(L"%hs HRESULT: 0x%08lx (%hs)\n", msg, hr, err.ErrorMessage());
}

// Function declarations
HRESULT InitializeMediaFoundation();
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType);
HRESULT ConfigureAudioMediaType(ComPtr<IMFSourceReader> pAudioSourceReader, ComPtr<IMFMediaType>& ppSelectedAudioType);
HRESULT ConfigureSinkWriter(
    ComPtr<IMFMediaType> pVideoType, 
    ComPtr<IMFMediaType> pAudioType,
    ComPtr<IMFSinkWriter>& ppSinkWriter, 
    DWORD& videoStreamIndex, 
    DWORD& audioStreamIndex
);
void CaptureFrames();
void StartRecording();

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

// Configure audio media type
HRESULT ConfigureAudioMediaType(ComPtr<IMFSourceReader> pAudioSourceReader, ComPtr<IMFMediaType>& ppSelectedAudioType) {
    HRESULT hr = MFCreateMediaType(&ppSelectedAudioType);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AUDIO_CHANNELS);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, AUDIO_BLOCK_ALIGNMENT);
    if (SUCCEEDED(hr)) hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_AVG_BYTES_PER_SECOND);
    if (SUCCEEDED(hr)) hr = pAudioSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, ppSelectedAudioType.Get());
    if (FAILED(hr)) PrintErrorMessage("Failed to configure audio media type.", hr);
    return hr;
}

// Configure Sink Writer for MP4 Output
HRESULT ConfigureSinkWriter(
    ComPtr<IMFMediaType> pVideoType, 
    ComPtr<IMFMediaType> pAudioType,
    ComPtr<IMFSinkWriter>& ppSinkWriter, 
    DWORD& videoStreamIndex, 
    DWORD& audioStreamIndex
) {
    HRESULT hr = MFCreateSinkWriterFromURL(L"output.mp4", NULL, NULL, &ppSinkWriter);

    ComPtr<IMFMediaType> pVideoMediaTypeOut;
    hr = MFCreateMediaType(&pVideoMediaTypeOut);
    if (SUCCEEDED(hr)) hr = pVideoMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = pVideoMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = pVideoMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, VIDEO_BITRATE);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(pVideoMediaTypeOut.Get(), MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_FRAME_RATE, FRAME_RATE_NUMERATOR, FRAME_RATE_DENOMINATOR);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = pVideoMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = ppSinkWriter->AddStream(pVideoMediaTypeOut.Get(), &videoStreamIndex);

    if (SUCCEEDED(hr) && pAudioType) {
        ComPtr<IMFMediaType> pAudioMediaTypeOut;
        hr = MFCreateMediaType(&pAudioMediaTypeOut);
        if (SUCCEEDED(hr)) hr = pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = ppSinkWriter->AddStream(pAudioMediaTypeOut.Get(), &audioStreamIndex);
        if (SUCCEEDED(hr)) hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
        if (SUCCEEDED(hr)) hr = ppSinkWriter->SetInputMediaType(audioStreamIndex, pAudioType.Get(), NULL);
    } else if (SUCCEEDED(hr)) {
        hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
    }

    if (SUCCEEDED(hr)) hr = ppSinkWriter->BeginWriting();
    if (FAILED(hr)) PrintErrorMessage("Failed to configure sink writer.", hr);
    return hr;
}

// Capture frames until stopped by Enter key press
void CaptureFrames() {
    printf("Capturing frames... Press Enter to stop recording.\n");
    HRESULT hr = S_OK;
    LONGLONG startTime = 0;

    auto keyPressThread = std::thread([]() {
        getchar();
        isRecording = false;
    });

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
            LONGLONG llSampleTime = MFGetSystemTime() - startTime;
            pVideoSample->SetSampleTime(llSampleTime);
            pVideoSample->SetSampleDuration(FRAME_DURATION);
            hr = pSinkWriter->WriteSample(videoStreamIndex, pVideoSample.Get());
            if (FAILED(hr)) PrintErrorMessage("Failed to write video sample.", hr);
        }

        // Capture Audio Sample
        if (pAudioSourceReader) {
            ComPtr<IMFSample> pAudioSample;
            DWORD audioStreamFlags = 0;
            hr = pAudioSourceReader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                NULL,
                &audioStreamFlags,
                NULL,
                &pAudioSample
            );

            if (audioStreamFlags & MF_SOURCE_READERF_STREAMTICK) {
                printf("Audio stream tick detected\n");
            }

            if (pAudioSample) {
                LONGLONG llAudioSampleTime = MFGetSystemTime() - startTime;
                pAudioSample->SetSampleTime(llAudioSampleTime);
                hr = pSinkWriter->WriteSample(audioStreamIndex, pAudioSample.Get());
                if (FAILED(hr)) PrintErrorMessage("Failed to write audio sample.", hr);
            }
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

    pVideoSourceReader.Reset();
    pAudioSourceReader.Reset();
}

// Start Recording
void StartRecording() {
    printf("Starting recording...\n");

    ComPtr<IMFAttributes> pVideoAttributes;
    HRESULT hr = MFCreateAttributes(&pVideoAttributes, 1);
    hr = pVideoAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppVideoDevicesRaw = nullptr;
    UINT32 videoDeviceCount = 0;
    hr = MFEnumDeviceSources(pVideoAttributes.Get(), &ppVideoDevicesRaw, &videoDeviceCount);

    if (videoDeviceCount == 0) {
        printf("No video capture devices found.\n");
        CoTaskMemFree(ppVideoDevicesRaw);
        return;
    }

    ComPtr<IMFActivate> pVideoDevice = ppVideoDevicesRaw[0];
    CoTaskMemFree(ppVideoDevicesRaw);
    hr = pVideoDevice->ActivateObject(IID_PPV_ARGS(&pVideoMediaSource));
    hr = MFCreateSourceReaderFromMediaSource(pVideoMediaSource.Get(), NULL, &pVideoSourceReader);

    ComPtr<IMFMediaType> pSelectedVideoType;
    hr = ConfigureConservativeMediaType(pVideoSourceReader, pSelectedVideoType);

    ComPtr<IMFAttributes> pAudioAttributes;
    hr = MFCreateAttributes(&pAudioAttributes, 1);
    hr = pAudioAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);

    IMFActivate** ppAudioDevicesRaw = nullptr;
    UINT32 audioDeviceCount = 0;
    hr = MFEnumDeviceSources(pAudioAttributes.Get(), &ppAudioDevicesRaw, &audioDeviceCount);

    ComPtr<IMFMediaType> pSelectedAudioType;
    if (audioDeviceCount == 0) {
        printf("No audio capture devices found. Proceeding without audio.\n");
    } else {
        ComPtr<IMFActivate> pAudioDevice = ppAudioDevicesRaw[0];
        CoTaskMemFree(ppAudioDevicesRaw);
        hr = pAudioDevice->ActivateObject(IID_PPV_ARGS(&pAudioMediaSource));
        hr = MFCreateSourceReaderFromMediaSource(pAudioMediaSource.Get(), NULL, &pAudioSourceReader);
        hr = ConfigureAudioMediaType(pAudioSourceReader, pSelectedAudioType);
    }

    hr = ConfigureSinkWriter(pSelectedVideoType, pSelectedAudioType, pSinkWriter, videoStreamIndex, audioStreamIndex);
    CaptureFrames();

    if (pSinkWriter) {
        hr = pSinkWriter->Finalize();
        pSinkWriter.Reset();
    }
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
