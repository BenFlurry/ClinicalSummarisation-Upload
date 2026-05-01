#pragma once
#include "AudioTranscriptionBridge.h"
#include <vector>
#include <mutex> 
#include <memory>
#include <filesystem>

#include "miniaudio.h" 

class AudioRecorder;

struct IAudioRecorderBackend {
    virtual ~IAudioRecorderBackend() = default;
    virtual bool Start(AudioRecorder& recorder) = 0;
    virtual void Stop(AudioRecorder& recorder) = 0;
    virtual std::filesystem::path ResolveOutputPath(const std::string& outputDirectoryOverride) = 0;
};

class AudioRecorder {
private:
    AudioTranscriptionBridge* m_bridge;
    ma_device m_device;
    ma_device_config m_config;
    std::vector<float> m_currentBuffer;
    bool m_isRecording = false;
    std::mutex m_bufferMutex;
    std::string m_microphoneName;
    std::string m_outputDirectoryOverride;

    void Flush(bool isLast);
    void SaveToWav(const std::vector<float>& audioData);
    // void LoadFromWav(std::string filename);

public:
    AudioRecorder(AudioTranscriptionBridge* bridgePtr);
    ~AudioRecorder();
    void SetBackendForTesting(std::shared_ptr<IAudioRecorderBackend> backend);
    void Start();
    void Stop();
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void SetMicrophoneName(std::string microphoneName);

    // override output directory for unit testing (bypasses ApplicationData)
    void SetOutputDirectory(const std::string& dir);

private:
    std::shared_ptr<IAudioRecorderBackend> m_testBackend;
};