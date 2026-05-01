#pragma once
#include <openvino/openvino.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <memory>

struct ISpeakerEncoderBackend {
    virtual ~ISpeakerEncoderBackend() = default;
    virtual void Initialize(const std::string& modelPath, const std::string& device) = 0;
    virtual std::vector<float> GetEmbedding(const std::vector<float>& audioBuffer) = 0;
};

class SpeakerEncoder {
public:
    SpeakerEncoder();
    ~SpeakerEncoder();

    void SetBackendForTesting(std::shared_ptr<ISpeakerEncoderBackend> backend);

    void Initialize(const std::string& modelPath, const std::string& device = "CPU");
    std::vector<float> GetEmbedding(const std::vector<float>& audioBuffer);

    static float CosineSimilarity(const std::vector<float>& vecA, const std::vector<float>& vecB);

    bool is_loaded;

private:
    ov::Core m_core;
    ov::CompiledModel m_model;
    ov::InferRequest m_request;
    std::shared_ptr<ISpeakerEncoderBackend> m_testBackend;
};
