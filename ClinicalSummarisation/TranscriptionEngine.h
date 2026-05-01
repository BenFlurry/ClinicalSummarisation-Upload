#pragma once
#include "AudioTranscriptionBridge.h"
#include <thread>
#include <atomic>
#include <sstream>
#include <filesystem>
#include <memory>

#include "openvino/genai/whisper_pipeline.hpp"
#include "SpeakerEncoder.h"

struct TranscribedChunk {
	std::string text;
	float startTs = 0.0f;
	float endTs = 0.0f;
};

struct ITranscriptionBackend {
	virtual ~ITranscriptionBackend() = default;
	virtual void InitialiseModel(const std::filesystem::path& modelPath) = 0;
	virtual std::vector<TranscribedChunk> Generate(const std::vector<float>& audioData) = 0;
	virtual std::vector<float> GetEmbedding(const std::vector<float>& audioData) = 0;
	virtual SpeakerEncoder* GetEncoder() = 0;
};

class TranscriptionEngine {
public:
	TranscriptionEngine(AudioTranscriptionBridge* bridgePtr, std::shared_ptr<ITranscriptionBackend> backend = nullptr);
	~TranscriptionEngine();
	void SetBackendForTesting(std::shared_ptr<ITranscriptionBackend> backend);

	void InitialiseModel();
	SpeakerEncoder* GetEncoder();
	std::string ProcessLoop();
	void SetDoctorProfile(const std::vector<float>& profile) { m_doctorProfile = profile;  }
	void Cancel();
	bool IsCancelled() const;

private:
	AudioTranscriptionBridge* m_bridge;
	SpeakerEncoder* m_speakerEncoder;
	std::stringstream m_fullTranscript;
	std::atomic<bool> m_isRunning;
	std::atomic<bool> m_isCancelled{ false };

	ov::genai::WhisperPipeline* m_pipeline = nullptr;
	std::vector<float> m_doctorProfile;
	std::shared_ptr<ITranscriptionBackend> m_testBackend;
	

};

