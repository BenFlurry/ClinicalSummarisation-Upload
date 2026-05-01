#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

class GuidelineRAG;

struct IndexProgress {
    int totalChunks;
    int processedChunks;
    std::string currentFile;
};

using ProgressCallback = std::function<void(const IndexProgress&)>;

class GuidelineIndexer {
public:
    GuidelineIndexer();
    ~GuidelineIndexer();

    // index a single PDF parse with MuPDF, embed chunks, write to SQLite + HNSW.
    int IndexPdf(const std::filesystem::path& pdfSourcePath, const std::filesystem::path& basePath, GuidelineRAG& rag, ProgressCallback progress = nullptr);

    void RemoveGuideline(const std::string& guidelineName, const std::filesystem::path& basePath, GuidelineRAG& rag); 

    // full rebuild of the HNSW index from SQLite
    void RebuildHnswFromSqlite(const std::filesystem::path& basePath, GuidelineRAG& rag); 

    static std::vector<std::string> GetIndexedGuidelines(const std::filesystem::path& basePath);
};
