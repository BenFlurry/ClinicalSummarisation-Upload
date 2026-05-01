#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <winrt/Windows.Foundation.h> 
#include "miniaudio.h" 

struct IDoctorEmbeddingBackend {
    virtual ~IDoctorEmbeddingBackend() = default;
    virtual std::vector<float> CaptureEnrollmentAudio(std::atomic<bool>& finishEarly, std::atomic<bool>& cancel) = 0;
    virtual std::string ResolveDefaultFilePath() = 0;
    virtual bool SaveProfile(const std::string& filePath, const std::vector<float>& embedding) = 0;
    virtual std::vector<float> LoadProfile(const std::string& filePath) = 0;
};

// declare so we can use
class SpeakerEncoder;

class DoctorEmbedding {
public:
    winrt::Windows::Foundation::IAsyncAction EnrollNewSpeakerAsync(SpeakerEncoder* encoder);
    void FinishEnrollmentEarly();
    void CancelEnrollment();
    void SetBackendForTesting(std::shared_ptr<IDoctorEmbeddingBackend> backend);

    std::vector<float> getSpeachEmbedding();
    bool IsProfileEnrolled();

    // override storage directory for unit testing (bypasses ApplicationData)
    void SetStorageDirectory(const std::string& dir);

private:
    std::vector<float> m_speachEmbedding;
    std::atomic<bool> m_finishEarly{ false };
    std::atomic<bool> m_cancel{ false };
    std::string m_storageDirectoryOverride;
    std::shared_ptr<IDoctorEmbeddingBackend> m_testBackend;

    // helpers
    void SaveToDisk(const std::vector<float>& embedding);
    std::vector<float> LoadFromDisk();
    std::string getFilePath();

    // mini audio callback context
    struct EnrollmentContext {
        std::vector<float> audioBuffer;
        bool isRecording = false;
        // 30s at 16kHz
        size_t maxSamples = 16000 * 30; 
    };

    // static callback for Miniaudio
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
};