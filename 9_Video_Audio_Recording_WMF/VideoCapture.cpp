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
const UINT64 FRAME_DURATION = 10'000'000 / FRAME_RATE_NUMERATOR; // 15 FPS in 100-nanosecond units
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
bool isRecording = true; // Flag for recording status

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

// Configure conservative media type
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType) {
    printf("Configuring conservative media type...\n");
    HRESULT hr = MFCreateMediaType(&ppSelectedType);
    hr = ppSelectedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = ppSelectedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12); 
    hr = MFSetAttributeSize(ppSelectedType.Get(), MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT);
    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_FRAME_RATE, FRAME_RATE_NUMERATOR, FRAME_RATE_DENOMINATOR);
    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, ppSelectedType.Get());
    return hr;
}

// Configure audio media type
HRESULT ConfigureAudioMediaType(ComPtr<IMFSourceReader> pAudioSourceReader, ComPtr<IMFMediaType>& ppSelectedAudioType) {
    printf("Configuring audio media type...\n");
    HRESULT hr = MFCreateMediaType(&ppSelectedAudioType);
    hr = ppSelectedAudioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    hr = ppSelectedAudioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, AUDIO_CHANNELS);
    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE);
    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, AUDIO_BLOCK_ALIGNMENT);
    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_AVG_BYTES_PER_SECOND);
    hr = pAudioSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, ppSelectedAudioType.Get());
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
    printf("Configuring sink writer for output.mp4...\n");
    HRESULT hr = MFCreateSinkWriterFromURL(L"output.mp4", NULL, NULL, &ppSinkWriter);
    
    // Configure video output type
    ComPtr<IMFMediaType> pVideoMediaTypeOut = nullptr;
    hr = MFCreateMediaType(&pVideoMediaTypeOut);
    hr = pVideoMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    hr = pVideoMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    hr = pVideoMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, VIDEO_BITRATE);
    hr = MFSetAttributeSize(pVideoMediaTypeOut.Get(), MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT);
    hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_FRAME_RATE, FRAME_RATE_NUMERATOR, FRAME_RATE_DENOMINATOR);
    hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = pVideoMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = ppSinkWriter->AddStream(pVideoMediaTypeOut.Get(), &videoStreamIndex);

    // Configure audio output type if provided
    if (pAudioType) {
        ComPtr<IMFMediaType> pAudioMediaTypeOut = nullptr;
        hr = MFCreateMediaType(&pAudioMediaTypeOut);
        hr = pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        hr = pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        hr = ppSinkWriter->AddStream(pAudioMediaTypeOut.Get(), &audioStreamIndex);
        hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
        hr = ppSinkWriter->SetInputMediaType(audioStreamIndex, pAudioType.Get(), NULL);
    } else {
        hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
    }

    hr = ppSinkWriter->BeginWriting();
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
        if (startTime == 0) {
            startTime = MFGetSystemTime();
        }

        ComPtr<IMFSample> pVideoSample = nullptr;
        DWORD videoStreamFlags = 0;
        hr = pVideoSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            NULL,
            &videoStreamFlags,
            NULL,
            &pVideoSample
        );

        if (pVideoSample) {
            LONGLONG llSampleTime = MFGetSystemTime() - startTime;
            pVideoSample->SetSampleTime(llSampleTime);
            pVideoSample->SetSampleDuration(FRAME_DURATION);
            hr = pSinkWriter->WriteSample(videoStreamIndex, pVideoSample.Get());
        }

        if (pAudioSourceReader) {
            ComPtr<IMFSample> pAudioSample = nullptr;
            DWORD audioStreamFlags = 0;
            hr = pAudioSourceReader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                NULL,
                &audioStreamFlags,
                NULL,
                &pAudioSample
            );

            if (pAudioSample) {
                LONGLONG llAudioSampleTime = MFGetSystemTime() - startTime;
                pAudioSample->SetSampleTime(llAudioSampleTime);
                hr = pSinkWriter->WriteSample(audioStreamIndex, pAudioSample.Get());
            }
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

    ComPtr<IMFMediaType> pSelectedVideoType = nullptr;
    hr = ConfigureConservativeMediaType(pVideoSourceReader, pSelectedVideoType);

    ComPtr<IMFAttributes> pAudioAttributes;
    hr = MFCreateAttributes(&pAudioAttributes, 1);
    hr = pAudioAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);

    IMFActivate** ppAudioDevicesRaw = nullptr;
    UINT32 audioDeviceCount = 0;
    hr = MFEnumDeviceSources(pAudioAttributes.Get(), &ppAudioDevicesRaw, &audioDeviceCount);

    ComPtr<IMFMediaType> pSelectedAudioType = nullptr;
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
    hr = InitializeMediaFoundation();
    StartRecording();
    MFShutdown();
    CoUninitialize();
    return 0;
}
