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

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

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

// Configure conservative media type (640x480 at 15 FPS, NV12 format)
HRESULT ConfigureConservativeMediaType(ComPtr<IMFSourceReader> pSourceReader, ComPtr<IMFMediaType>& ppSelectedType) {
    printf("Configuring conservative media type...\n");
    HRESULT hr = MFCreateMediaType(&ppSelectedType);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create media type.", hr);
        return hr;
    }

    hr = ppSelectedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set major type for video.", hr);
        return hr;
    }

    hr = ppSelectedType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12); // Widely-supported uncompressed format
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set subtype for video.", hr);
        return hr;
    }

    hr = MFSetAttributeSize(ppSelectedType.Get(), MF_MT_FRAME_SIZE, 640, 480);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set frame size for video.", hr);
        return hr;
    }

    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_FRAME_RATE, 15, 1); // 15 FPS for compatibility
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set frame rate for video.", hr);
        return hr;
    }

    hr = MFSetAttributeRatio(ppSelectedType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set pixel aspect ratio for video.", hr);
        return hr;
    }

    hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, ppSelectedType.Get());
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set current media type on video source reader.", hr);
        return hr;
    }

    return hr;
}

// Configure audio media type (e.g., 48000 Hz, 2 channels, PCM)
HRESULT ConfigureAudioMediaType(ComPtr<IMFSourceReader> pAudioSourceReader, ComPtr<IMFMediaType>& ppSelectedAudioType) {
    printf("Configuring audio media type...\n");
    HRESULT hr = MFCreateMediaType(&ppSelectedAudioType);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create audio media type.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set major type for audio.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM); // PCM format
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set subtype for audio.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2); // Stereo
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set number of audio channels.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000); // 48 kHz
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set audio samples per second.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16); // 16-bit
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set audio bits per sample.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4); // 2 channels * 16 bits / 8
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set audio block alignment.", hr);
        return hr;
    }

    hr = ppSelectedAudioType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 2 * 2); // 48k * 2 channels * 2 bytes
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set audio avg bytes per second.", hr);
        return hr;
    }

    hr = pAudioSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, ppSelectedAudioType.Get());
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set current media type on audio source reader.", hr);
        return hr;
    }

    return hr;
}

// Configure Sink Writer for MP4 Output with both video and audio
HRESULT ConfigureSinkWriter(
    ComPtr<IMFMediaType> pVideoType, 
    ComPtr<IMFMediaType> pAudioType,
    ComPtr<IMFSinkWriter>& ppSinkWriter, 
    DWORD& videoStreamIndex, 
    DWORD& audioStreamIndex
) {
    printf("Configuring sink writer for output.mp4...\n");
    HRESULT hr = MFCreateSinkWriterFromURL(L"output.mp4", NULL, NULL, &ppSinkWriter);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create sink writer.", hr);
        return hr;
    }

    // Configure video output type
    ComPtr<IMFMediaType> pVideoMediaTypeOut = nullptr;
    hr = MFCreateMediaType(&pVideoMediaTypeOut);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create video output media type.", hr);
        return hr;
    }
    hr = pVideoMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video major type for sink writer.", hr);
        return hr;
    }
    hr = pVideoMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264); // MP4-compatible H.264
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video subtype for sink writer.", hr);
        return hr;
    }
    hr = pVideoMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, 800000);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video average bitrate for sink writer.", hr);
        return hr;
    }
    hr = MFSetAttributeSize(pVideoMediaTypeOut.Get(), MF_MT_FRAME_SIZE, 640, 480);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video frame size for sink writer.", hr);
        return hr;
    }
    hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_FRAME_RATE, 15, 1); // 15 FPS
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video frame rate for sink writer.", hr);
        return hr;
    }
    hr = MFSetAttributeRatio(pVideoMediaTypeOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video pixel aspect ratio for sink writer.", hr);
        return hr;
    }
    hr = pVideoMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video interlace mode for sink writer.", hr);
        return hr;
    }

    hr = ppSinkWriter->AddStream(pVideoMediaTypeOut.Get(), &videoStreamIndex);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to add video stream to sink writer.", hr);
        return hr;
    }

    // Configure audio output type only if audioType is provided
    if (pAudioType) {
        ComPtr<IMFMediaType> pAudioMediaTypeOut = nullptr;
        hr = MFCreateMediaType(&pAudioMediaTypeOut);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to create audio output media type.", hr);
            return hr;
        }
        hr = pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set audio major type for sink writer.", hr);
            return hr;
        }
        hr = pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC); // MP4-compatible AAC
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set audio subtype for sink writer.", hr);
            return hr;
        }

        hr = ppSinkWriter->AddStream(pAudioMediaTypeOut.Get(), &audioStreamIndex);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to add audio stream to sink writer.", hr);
            return hr;
        }

        // Set input media types for sink writer
        hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set video input media type for sink writer.", hr);
            return hr;
        }

        hr = ppSinkWriter->SetInputMediaType(audioStreamIndex, pAudioType.Get(), NULL);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set audio input media type for sink writer.", hr);
            return hr;
        }
    } else {
        // Only video stream
        hr = ppSinkWriter->SetInputMediaType(videoStreamIndex, pVideoType.Get(), NULL);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to set video input media type for sink writer.", hr);
            return hr;
        }
    }

    // Begin writing
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
    LONGLONG startTime = 0; // Track start time for consistent timestamps

    // Start a thread to detect Enter key press
    auto keyPressThread = std::thread([]() {
        getchar();
        isRecording = false; // Stop recording when Enter is pressed
    });

    while (isRecording) {
        // Set the start time if it's the first sample
        if (startTime == 0) {
            startTime = MFGetSystemTime();
        }

        // Read video sample
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

        if (FAILED(hr)) {
            PrintErrorMessage("Failed to read video sample.", hr);
            break;
        }

        if (videoStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            printf("End of video stream reached.\n");
            break;
        }

        if (pVideoSample) {
            // Set sample time relative to start time
            LONGLONG llSampleTime = MFGetSystemTime() - startTime;
            pVideoSample->SetSampleTime(llSampleTime);

            // Set duration according to frame rate (15 FPS)
            LONGLONG llFrameDuration = (LONGLONG)(10'000'000.0 / 15);
            pVideoSample->SetSampleDuration(llFrameDuration);

            // Write video sample to sink writer
            hr = pSinkWriter->WriteSample(videoStreamIndex, pVideoSample.Get());
            if (FAILED(hr)) {
                PrintErrorMessage("Failed to write video sample to sink writer.", hr);
                break;
            }
        }

        // Process audio if available
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

            if (FAILED(hr)) {
                PrintErrorMessage("Failed to read audio sample.", hr);
                break;
            }

            if (audioStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
                printf("End of audio stream reached.\n");
                break;
            }

            if (pAudioSample) {
                // Set sample time relative to start time
                LONGLONG llAudioSampleTime = MFGetSystemTime() - startTime;
                pAudioSample->SetSampleTime(llAudioSampleTime);

                // Write audio sample to sink writer without modifying sample time
                hr = pSinkWriter->WriteSample(audioStreamIndex, pAudioSample.Get());
                if (FAILED(hr)) {
                    PrintErrorMessage("Failed to write audio sample to sink writer.", hr);
                    break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (keyPressThread.joinable()) keyPressThread.join();
    printf("Finished capturing frames.\n");
}

// Start Recording
void StartRecording() {
    printf("Starting recording...\n");

    // Enumerate video devices
    ComPtr<IMFAttributes> pVideoAttributes;
    HRESULT hr = MFCreateAttributes(&pVideoAttributes, 1);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create video attributes.", hr);
        return;
    }

    hr = pVideoAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set video device source type.", hr);
        return;
    }

    IMFActivate** ppVideoDevicesRaw = nullptr;
    UINT32 videoDeviceCount = 0;
    hr = MFEnumDeviceSources(pVideoAttributes.Get(), &ppVideoDevicesRaw, &videoDeviceCount);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to enumerate video devices.", hr);
        return;
    }

    if (videoDeviceCount == 0) {
        printf("No video capture devices found.\n");
        CoTaskMemFree(ppVideoDevicesRaw);
        return;
    }

    // Select the first video device
    ComPtr<IMFActivate> pVideoDevice = ppVideoDevicesRaw[0];
    CoTaskMemFree(ppVideoDevicesRaw);

    hr = pVideoDevice->ActivateObject(IID_PPV_ARGS(&pVideoMediaSource));
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to activate video device.", hr);
        return;
    }

    hr = MFCreateSourceReaderFromMediaSource(pVideoMediaSource.Get(), NULL, &pVideoSourceReader);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create video source reader.", hr);
        return;
    }

    // Configure video media type
    ComPtr<IMFMediaType> pSelectedVideoType = nullptr;
    hr = ConfigureConservativeMediaType(pVideoSourceReader, pSelectedVideoType);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to configure video media type.", hr);
        return;
    }

    // Enumerate audio devices
    ComPtr<IMFAttributes> pAudioAttributes;
    hr = MFCreateAttributes(&pAudioAttributes, 1);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to create audio attributes.", hr);
        return;
    }

    hr = pAudioAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID
    );
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to set audio device source type.", hr);
        return;
    }

    IMFActivate** ppAudioDevicesRaw = nullptr;
    UINT32 audioDeviceCount = 0;
    hr = MFEnumDeviceSources(pAudioAttributes.Get(), &ppAudioDevicesRaw, &audioDeviceCount);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to enumerate audio devices.", hr);
        CoTaskMemFree(ppAudioDevicesRaw);
        return;
    }

    ComPtr<IMFMediaType> pSelectedAudioType = nullptr;
    if (audioDeviceCount == 0) {
        printf("No audio capture devices found. Proceeding without audio.\n");
    } else {
        // Select the first audio device
        ComPtr<IMFActivate> pAudioDevice = ppAudioDevicesRaw[0];
        CoTaskMemFree(ppAudioDevicesRaw);
        hr = pAudioDevice->ActivateObject(IID_PPV_ARGS(&pAudioMediaSource));
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to activate audio device.", hr);
            return;
        }

        hr = MFCreateSourceReaderFromMediaSource(pAudioMediaSource.Get(), NULL, &pAudioSourceReader);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to create audio source reader.", hr);
            return;
        }

        // Configure audio media type
        hr = ConfigureAudioMediaType(pAudioSourceReader, pSelectedAudioType);
        if (FAILED(hr)) {
            PrintErrorMessage("Failed to configure audio media type.", hr);
            return;
        }
    }

    // Configure Sink Writer with both video and audio (if available)
    hr = ConfigureSinkWriter(pSelectedVideoType, pSelectedAudioType, pSinkWriter, videoStreamIndex, audioStreamIndex);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to configure sink writer.", hr);
        return;
    }

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
    // Initialize COM library
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintErrorMessage("Failed to initialize COM library.", hr);
        return -1;
    }

    // Initialize Media Foundation
    hr = InitializeMediaFoundation();
    if (FAILED(hr)) {
        CoUninitialize();
        return -1;
    }

    // Start Recording
    StartRecording();

    // Shutdown Media Foundation
    MFShutdown();

    // Uninitialize COM library
    CoUninitialize();
    return 0;
}
