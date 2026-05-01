#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

struct GuidelineResult {
    int chunkId;
    std::string guidelineName;
    int pageNumber;
    float bboxX0, bboxY0, bboxX1, bboxY1;
    std::string chunkText;
    float distance;
};

struct IGuidelineRAGBackend {
    virtual ~IGuidelineRAGBackend() = default;
    virtual void Initialize(const std::filesystem::path& modelBasePath, const std::filesystem::path& dataPath) = 0;
    virtual std::vector<float> EmbedText(const std::string& text) = 0;
    virtual std::vector<GuidelineResult> Query(const std::string& summaryText, int topK) = 0;
    virtual void ReloadIndex() = 0;
    virtual bool HasIndex() const = 0;
};

class GuidelineRAG {
public:
    GuidelineRAG();
    ~GuidelineRAG();

    // Test seam: inject backend implementation.
    void SetBackendForTesting(std::shared_ptr<IGuidelineRAGBackend> backend);

    void Initialize(const std::filesystem::path& modelBasePath,
                    const std::filesystem::path& dataPath);

    std::vector<GuidelineResult> Query(const std::string& summaryText, int topK = 3);

    // exposed so GuidelineIndexer can reuse the loaded MedCPT model
    std::vector<float> EmbedText(const std::string& text);

    // reload the HNSW index and SQLite database from disk after indexing and removing docs
    void ReloadIndex();

    // returns true if the HNSW index and SQLite database are loaded and the index has elements.
    bool HasIndex() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    std::filesystem::path m_basePath;
    std::filesystem::path m_dataPath;

    std::shared_ptr<IGuidelineRAGBackend> m_testBackend;
};
