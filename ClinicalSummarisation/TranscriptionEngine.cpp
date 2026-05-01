#include "pch.h"
#include "TranscriptionEngine.h"
#include "Helpers.h"
#include "AudioUtils.h"
#include <iostream>

namespace {
    class DefaultTranscriptionBackend final : public ITranscriptionBackend {
    public:
        DefaultTranscriptionBackend() {
            std::string encoderPath = Helpers::GetModelPath("speaker_encoder_int8/openvino_model.xml").string();
            m_speakerEncoder = std::make_unique<SpeakerEncoder>();
            m_speakerEncoder->Initialize(encoderPath, "CPU");
        }

        void InitialiseModel(const std::filesystem::path& modelPath) override {
            m_pipeline = std::make_unique<ov::genai::WhisperPipeline>(modelPath, "CPU");
        }

        std::vector<TranscribedChunk> Generate(const std::vector<float>& audioData) override {
            ov::genai::WhisperGenerationConfig config;
            config.max_new_tokens = 100;
            config.task = "transcribe";
            config.return_timestamps = true;
            config.num_beams = 3;

            auto result = m_pipeline->generate(audioData, config);
            std::vector<TranscribedChunk> chunks;
            chunks.reserve(result.chunks->size());

            for (const auto& chunk : *result.chunks) {
                chunks.push_back({ chunk.text, chunk.start_ts, chunk.end_ts });
            }

            return chunks;
        }

        std::vector<float> GetEmbedding(const std::vector<float>& audioData) override {
            return m_speakerEncoder->GetEmbedding(audioData);
        }

        SpeakerEncoder* GetEncoder() override {
            return m_speakerEncoder.get();
        }

    private:
        std::unique_ptr<ov::genai::WhisperPipeline> m_pipeline;
        std::unique_ptr<SpeakerEncoder> m_speakerEncoder;
    };
}

// connect to bridge
TranscriptionEngine::TranscriptionEngine(AudioTranscriptionBridge* bridgePtr, std::shared_ptr<ITranscriptionBackend> backend) {
	m_bridge = bridgePtr;
	m_isRunning = false;
	m_testBackend = backend ? backend : std::make_shared<DefaultTranscriptionBackend>();
	m_speakerEncoder = m_testBackend->GetEncoder();
}

TranscriptionEngine::~TranscriptionEngine() {
	m_pipeline = nullptr;
}

void TranscriptionEngine::InitialiseModel() {
	std::filesystem::path modelPath = Helpers::GetModelPath("whisper-medium-i4");
	m_testBackend->InitialiseModel(modelPath);
}

void TranscriptionEngine::SetBackendForTesting(std::shared_ptr<ITranscriptionBackend> backend) {
    m_testBackend = std::move(backend);
    m_speakerEncoder = m_testBackend ? m_testBackend->GetEncoder() : nullptr;
}

SpeakerEncoder* TranscriptionEngine::GetEncoder() {
    return m_testBackend ? m_testBackend->GetEncoder() : m_speakerEncoder;
}

// main running loop which takes in the audio chunk, transcribes and diarises
std::string TranscriptionEngine::ProcessLoop() {
    // clear previous text
    m_fullTranscript.str("");
    m_isRunning = true;
    m_isCancelled = false;

    while (m_isRunning) {
        AudioChunk audioChunk = m_bridge->Pop();

        if (m_isCancelled) {
            break;
        }

		AudioUtils::NormaliseAudio(audioChunk.audioData);


		auto generatedChunks = m_testBackend->Generate(audioChunk.audioData);

        // as generation is a blocking operation, check again if cancelled
        if (m_isCancelled) {
            break;
        }

		std::vector<float>& sourceAudio = audioChunk.audioData;

		for (const auto& transcribedChunk : generatedChunks) {

			size_t startIdx = (size_t)(transcribedChunk.startTs * 16000);
			size_t endIdx = (size_t)(transcribedChunk.endTs * 16000);

			if (startIdx >= sourceAudio.size()) startIdx = sourceAudio.size() - 1;
			if (endIdx > sourceAudio.size()) endIdx = sourceAudio.size();
			if (endIdx <= startIdx) continue; 

			// Extract Audio Clip
			std::vector<float> clip(sourceAudio.begin() + startIdx, sourceAudio.begin() + endIdx);

			std::string speakerLabel = "[Unknown]";

			if (clip.size() > 8000) {

				float sumSquares = 0.0f;
				for (float sample : clip) {
					sumSquares += sample * sample;
				}

				float rms = std::sqrt(sumSquares / clip.size());
				
				std::vector<float> currentEmbedding = m_testBackend->GetEmbedding(clip);

				// compare with doctor
				float score = SpeakerEncoder::CosineSimilarity(m_doctorProfile, currentEmbedding);
				std::string scoreStr = std::to_string(score).substr(0, 4); // "0.85"

				// 0.5 - 0.7 is a good range
				if (score > 0.7f) {
					speakerLabel = "Doctor";
					//speakerLabel = "Doctor " + scoreStr;
				}
				else {
					speakerLabel = "Patient";
					//speakerLabel = "Patient " + scoreStr;
				}
			}
			m_fullTranscript << "[" << speakerLabel << "] " << transcribedChunk.text << "\n";

			if (audioChunk.isLastChunk) {
				m_isRunning = false;
			}
		}
    }
    if (m_isCancelled) {
        return "";
    }

    return m_fullTranscript.str();
}

void TranscriptionEngine::Cancel() {
    m_isCancelled = true;
    m_isRunning = false;
}

bool TranscriptionEngine::IsCancelled() const {
    return m_isCancelled;
}
