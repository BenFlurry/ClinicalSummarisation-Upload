#pragma once

#include "openvino/genai/llm_pipeline.hpp"
#include <filesystem>
#include <memory>

struct ISummarisationBackend {
    virtual ~ISummarisationBackend() = default;
    virtual void LoadModel(const std::filesystem::path& modelPath) = 0;
    virtual std::string Generate(const std::string& prompt, int maxNewTokens, float temperature) = 0;
};

class SummarisationEngine {
private:
	ov::genai::LLMPipeline* m_model = nullptr;
	std::shared_ptr<ISummarisationBackend> m_testBackend;

public:
	void loadModel();
	std::string GenerateFromTranscription(std::string transcript);
	void SetBackendForTesting(std::shared_ptr<ISummarisationBackend> backend);
};

