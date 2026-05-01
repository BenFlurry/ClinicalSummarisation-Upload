#pragma once

#include "GuidelineRAG.h"
#include "SummarisationEngine.h"
#include "TranscriptionEngine.h"
#include "SpeakerEncoder.h"
#include "AudioRecorder.h"
#include "DoctorEmbedding.h"

#include <filesystem>
#include <string>
#include <vector>
#include <atomic>

namespace test_fakes {
    struct RagBackend final : IGuidelineRAGBackend {
        bool initialized = false;
        bool hasIndex = false;
        bool reloaded = false;
        std::string lastEmbedText;
        std::string lastQueryText;
        int lastTopK = -1;
        std::vector<float> embed = { 0.1f, 0.2f, 0.3f };
        std::vector<GuidelineResult> queryResults;

        void Initialize(const std::filesystem::path&, const std::filesystem::path&) override { initialized = true; }
        std::vector<float> EmbedText(const std::string& text) override { lastEmbedText = text; return embed; }
        std::vector<GuidelineResult> Query(const std::string& text, int topK) override { lastQueryText = text; lastTopK = topK; return queryResults; }
        void ReloadIndex() override { reloaded = true; }
        bool HasIndex() const override { return hasIndex; }
    };

    struct SummarisationBackend final : ISummarisationBackend {
        std::filesystem::path loadedModelPath;
        std::string lastPrompt;
        int lastMaxTokens = 0;
        float lastTemperature = 0.0f;
        std::string response = "mock-summary";

        void LoadModel(const std::filesystem::path& modelPath) override { loadedModelPath = modelPath; }
        std::string Generate(const std::string& prompt, int maxNewTokens, float temperature) override {
            lastPrompt = prompt;
            lastMaxTokens = maxNewTokens;
            lastTemperature = temperature;
            return response;
        }
    };

    struct TranscriptionBackend final : ITranscriptionBackend {
        std::vector<TranscribedChunk> generatedChunks;
        std::vector<float> embeddingToReturn{ 1.0f, 0.0f };
        int generateCalls = 0;
        std::filesystem::path initialisedModelPath;
        SpeakerEncoder encoder;

        void InitialiseModel(const std::filesystem::path& modelPath) override { initialisedModelPath = modelPath; }
        std::vector<TranscribedChunk> Generate(const std::vector<float>&) override { ++generateCalls; return generatedChunks; }
        std::vector<float> GetEmbedding(const std::vector<float>&) override { return embeddingToReturn; }
        SpeakerEncoder* GetEncoder() override { return &encoder; }
    };

    struct SpeakerEncoderBackend final : ISpeakerEncoderBackend {
        std::string initModel;
        std::string initDevice;
        std::vector<float> embedding{ 0.4f, 0.5f };
        std::vector<float> lastInput;

        void Initialize(const std::string& modelPath, const std::string& device) override { initModel = modelPath; initDevice = device; }
        std::vector<float> GetEmbedding(const std::vector<float>& audioBuffer) override { lastInput = audioBuffer; return embedding; }
    };

    struct AudioRecorderBackend final : IAudioRecorderBackend {
        bool startCalled = false;
        bool stopCalled = false;
        int startCount = 0;
        int stopCount = 0;
        std::filesystem::path resolvedPath;

        bool Start(AudioRecorder&) override { startCalled = true; ++startCount; return true; }
        void Stop(AudioRecorder&) override { stopCalled = true; ++stopCount; }
        std::filesystem::path ResolveOutputPath(const std::string&) override { return resolvedPath; }
    };

    struct DoctorEmbeddingBackend final : IDoctorEmbeddingBackend {
        std::vector<float> capturedAudio{ 1.0f, 2.0f, 3.0f };
        std::string defaultPath = "C:\\Fake\\doctor_voice.dat";
        std::string lastSavedPath;
        std::vector<float> lastSavedProfile;
        std::vector<float> loadedProfile{ 7.0f, 8.0f, 9.0f };

        std::vector<float> CaptureEnrollmentAudio(std::atomic<bool>&, std::atomic<bool>&) override { return capturedAudio; }
        std::string ResolveDefaultFilePath() override { return defaultPath; }
        bool SaveProfile(const std::string& filePath, const std::vector<float>& embedding) override {
            lastSavedPath = filePath;
            lastSavedProfile = embedding;
            return true;
        }
        std::vector<float> LoadProfile(const std::string& filePath) override {
            lastSavedPath = filePath;
            return loadedProfile;
        }
    };

    inline std::filesystem::path TempDir() {
        auto p = std::filesystem::temp_directory_path() / ("ClinicalSummarisationTests_" + std::to_string(::GetTickCount64()));
        std::filesystem::create_directories(p);
        return p;
    }
}
